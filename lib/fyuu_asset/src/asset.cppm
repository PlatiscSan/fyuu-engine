module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <deque>

#include <type_traits>
#include <atomic>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#include <functional>

#include <shared_mutex>

#include <stop_token>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#if defined(__cpp_lib_reflection)
#include <meta>
#endif // defined(__cpp_lib_reflection)

#endif // !defined(__cpp_lib_modules)
#include <boost/intrusive_ptr.hpp>
#include <boost/type_index.hpp>
#include <boost/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_hash.hpp>
#include <boost/uuid/uuid_io.hpp>
#if !defined(__cpp_lib_reflection)
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#endif // !defined(__cpp_lib_reflection)
#include <nlohmann/json.hpp>
#include "log.hpp"
export module fyuu_asset:asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :log;
import :base_asset;

namespace fs = std::filesystem;

namespace {

	using namespace fyuu_asset;

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
	using SerializeTask = std::move_only_function<void()>;
#else
	using SerializeTask = std::function<void()>;
#endif

	template <class F>
	void AsyncSerialize(F&& task) {
		using TaskQueue = std::deque<SerializeTask>;

		// These objects are initialized synchronously, in declaration order. The
		// worker is destroyed first at shutdown, so it has joined before its queue
		// and synchronization objects are destroyed.
		static TaskQueue tasks;
		static std::mutex mutex;
		static std::condition_variable_any condition;
		static std::jthread ser_thread(
			[](std::stop_token token) noexcept {
				while (!token.stop_requested()) {
					SerializeTask current;

					{
						std::unique_lock<std::mutex> lock(mutex);
						condition.wait(
							lock,
							token,
							[] { return !tasks.empty(); }
						);

						// A stop request only terminates the thread after all tasks accepted
						// before shutdown have been drained.
						if (tasks.empty()) {
							return;
						}

						current = std::move(tasks.front());
						tasks.pop_front();
					}

					try {
						current();
					}
					catch (std::exception const& ex) {
						LOG_WARNING(
							std::format("Some error occurred while serializing the asset: {}", ex.what())
						)
					}
					catch (...) {
						LOG_WARNING("An unknown error occurred while serializing the asset")
					}
				}
			}
		);

		{
			std::lock_guard<std::mutex> lock(mutex);
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
			tasks.emplace_back(std::forward<F>(task));
#else
			using Function = std::decay_t<F>;
			if constexpr (std::is_copy_constructible_v<Function>) {
				tasks.emplace_back(std::forward<F>(task));
			}
			else {
				// C++20 std::function needs a copyable target. Only the fallback pays
				// for shared storage when the submitted callable itself is move-only.
				auto owned = std::make_shared<Function>(std::forward<F>(task));
				tasks.emplace_back([owned = std::move(owned)]() mutable {
					std::invoke(*owned);
				});
			}
#endif
		}
		condition.notify_one();
	}

}

namespace fyuu_asset {
	namespace execution {
		class AssetLoader;
	}

	export template <class T> class Asset final {
	public:
		using ManagedAsset = boost::intrusive_ptr<Asset>;

	private:
		// Lifetime is split into two phases:
		//   strong == 0: destroy T, but keep this allocation for existing Weak objects.
		//   weak   == 0: release the Asset allocation itself.
		// The strong-reference set owns one implicit weak reference, so the allocation
		// cannot disappear while any ManagedAsset still exists.

		std::atomic_size_t m_strong;
		std::atomic_size_t m_weak;
		boost::uuids::uuid m_id;

		// T must die with the last strong reference, potentially before the Asset
		// allocation can be released. The union disables automatic destruction, so
		// intrusive_ptr_release() can destroy T exactly once. After that, only the
		// counters and Weak operations remain valid; Get() requires a live ManagedAsset.

		union {
			T m_data;
		};

		template <class... Args>
		explicit Asset(boost::uuids::uuid const& id, Args&&... args)
			noexcept(std::is_nothrow_constructible_v<T, Args...>)
			: m_strong(1u),
			m_weak(1u),
			m_id(id),
			m_data(std::forward<Args>(args)...) {
		}

	public:
		class Weak {
		private:
			friend class Asset;

			Asset* m_asset = nullptr;

			void AddRef() noexcept {
				if (m_asset) {
					m_asset->m_weak.fetch_add(1, std::memory_order_relaxed);
				}
			}

			void Release() noexcept {
				if (m_asset && m_asset->m_weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
					delete m_asset;
				}
			}

		public:
			Weak() noexcept = default;

			explicit Weak(ManagedAsset const& asset) noexcept
				: m_asset(asset.get()) {
				AddRef();
			}

			Weak(Weak const& other) noexcept
				: m_asset(other.m_asset) {
				AddRef();
			}

			Weak(Weak&& other) noexcept
				: m_asset(std::exchange(other.m_asset, nullptr)) {
			}

			~Weak() {
				Release();
			}

			Weak& operator=(Weak const& other) noexcept {
				if (this != &other) {
					Weak replacement(other);
					std::swap(m_asset, replacement.m_asset);
				}
				return *this;
			}

			Weak& operator=(Weak&& other) noexcept {
				if (this != &other) {
					Release();
					m_asset = std::exchange(other.m_asset, nullptr);
				}
				return *this;
			}

			void Reset() noexcept {
				Release();
				m_asset = nullptr;
			}

			[[nodiscard]] bool Expired() const noexcept {
				return !m_asset || m_asset->m_strong.load(std::memory_order_acquire) == 0;
			}

			[[nodiscard]] ManagedAsset Lock() const noexcept {
				if (!m_asset) {
					return {};
				}

				// Checking then incrementing in separate operations would race with the
				// last ManagedAsset release. CAS increments only while strong is still nonzero.
				auto count = m_asset->m_strong.load(std::memory_order_acquire);
				while (count != 0) {
					if (m_asset->m_strong.compare_exchange_weak(
						count,
						count + 1,
						std::memory_order_acquire,
						std::memory_order_relaxed)) {
						return ManagedAsset{ m_asset, false };
					}
				}

				return {};
			}
		};

	private:
		friend class execution::AssetLoader;

		inline static std::unordered_map<boost::uuids::uuid, Weak> s_loaded_assets;
		inline static std::shared_mutex s_mutex;

		static void Unregister(Asset* asset) noexcept {
			std::unique_lock lock(s_mutex);
			auto found = s_loaded_assets.find(asset->m_id);
			if (found != s_loaded_assets.end() && found->second.m_asset == asset) {
				s_loaded_assets.erase(found);
			}
		}

		template <class... Args>
		[[nodiscard]] static ManagedAsset CreateWithID(boost::uuids::uuid const& id, Args&&... args) {
			// The Asset object, both counters, UUID and T share this one allocation.
			ManagedAsset asset{
				new Asset(id, std::forward<Args>(args)...),
				false
			};

			std::unique_lock<std::shared_mutex> lock(s_mutex);
			auto found = s_loaded_assets.find(asset->m_id);
			if (found != s_loaded_assets.end()) {
				if (auto loaded = found->second.Lock()) {
					return loaded;
				}
				s_loaded_assets.erase(found);
			}

			s_loaded_assets.emplace(asset->m_id, Weak{ asset });
			return asset;
		}

	public:
		Asset(Asset const&) = delete;
		Asset& operator=(Asset const&) = delete;
		Asset(Asset&&) = delete;
		Asset& operator=(Asset&&) = delete;
		
		// Intentionally empty: m_data is destroyed manually at strong == 0.
		~Asset() noexcept {
		}

		template <class... Args>
		[[nodiscard]] static ManagedAsset Create(Args&&... args) {
			return CreateWithID(
				boost::uuids::random_generator{}(),
				std::forward<Args>(args)...
			);
		}

		[[nodiscard]] static ManagedAsset Find(boost::uuids::uuid const& id) noexcept {
			std::shared_lock lock(s_mutex);
			auto found = s_loaded_assets.find(id);
			return found == s_loaded_assets.end() ? ManagedAsset{} : found->second.Lock();
		}

		static void CollectExpired() {
			std::unique_lock lock(s_mutex);
			std::erase_if(
				s_loaded_assets, 
				[](auto const& entry) {
					return entry.second.Expired();
				}
			);
		}

		[[nodiscard]] boost::uuids::uuid GetID() const noexcept {
			return m_id;
		}

		[[nodiscard]] decltype(auto) Get(this auto&& self) noexcept {
			return self.m_data;
		}

	private:
		friend void intrusive_ptr_add_ref(Asset* asset) noexcept {
			asset->m_strong.fetch_add(1, std::memory_order_relaxed);
		}

		friend void intrusive_ptr_release(Asset* asset) noexcept {
			if (asset->m_strong.fetch_sub(1, std::memory_order_acq_rel) != 1) {
				return;
			}

			// strong is now zero, so Weak::Lock() can no longer resurrect the payload.
			// Remove the registry's Weak before destroying T. Unregister() compares the
			// object address as well as the UUID, so an older instance cannot erase a
			// newer instance that reused the same UUID.
			Unregister(asset);
			AsyncSerialize(
				[id = asset->GetID(), data = std::move(asset->m_data)]() {
					std::string structure_name;
					nlohmann::json serialized = nlohmann::json::object();

#if defined(__cpp_lib_reflection)
					structure_name = std::meta::identifier_of(^^T);
					constexpr auto context = std::meta::access_context::current();
					template for (constexpr auto member : std::define_static_array(
						std::meta::nonstatic_data_members_of(^^T, context))) {
						if constexpr (std::meta::has_identifier(member)) {
							serialized[std::meta::identifier_of(member)] = data.[:member:];
						}
					}
#else
					structure_name = boost::typeindex::type_id<T>().pretty_name();
					if (auto position = structure_name.rfind("::");
						position != std::string::npos) {
						structure_name.erase(0, position + 2);
					}

					using Members = boost::describe::describe_members<
						T,
						boost::describe::mod_public
					>;
					boost::mp11::mp_for_each<Members>(
						[&](auto member) {
							serialized[member.name] = data.*member.pointer;
						}
					);
#endif
					auto path = GetPath(
						fs::path{ "conf" } / structure_name / (boost::uuids::to_string(id) + ".json")
					);
					fs::create_directories(path.parent_path());

					auto temporary = path;
					temporary += ".tmp";

					{
						std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
						if (!output) {
							throw std::runtime_error(
								std::format("Failed to open asset file '{}'", temporary.string())
							);
						}
						output << serialized.dump(2);
						output.flush();
						if (!output) {
							throw std::runtime_error(
								std::format("Failed to write asset file '{}'", temporary.string())
							);
						}
					}

					std::error_code error;
					fs::rename(temporary, path, error);
					if (error) {
						fs::remove(path, error);
						error.clear();
						fs::rename(temporary, path, error);
					}
					if (error) {
						throw std::runtime_error(
							std::format(
								"Failed to publish asset file '{}': {}",
								path.string(),
								error.message()
							)
						);
					}
				}
			);
			asset->m_data.~T();

			// Release the implicit weak reference owned by the former strong set. Real
			// Weak objects may continue to keep the allocation (but not T) alive.
			if (asset->m_weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				delete asset;
			}
		}
	};

} // namespace fyuu_asset
