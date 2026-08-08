module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <type_traits>
#include <utility>

#include <memory>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <deque>
#include <unordered_map>
#include <vector>

#include <exception>
#include <stdexcept>
#include <system_error>

#include <functional>

#include <fstream>
#include <string>

#include <filesystem>
#include <shared_mutex>
#include <string_view>

#include <coroutine>
#include <stop_token>
#include <format>

#if defined(__cpp_lib_reflection)
#include <meta>
#endif // defined(__cpp_lib_reflection)

#endif // !defined(__cpp_lib_modules)
#include <coroutine>
#include <boost/intrusive_ptr.hpp>
#include <boost/type_index.hpp>
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
import :uuid;
import plastic.serial_task;

namespace fs = std::filesystem;

namespace {
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
	using SerializeFunction = std::move_only_function<void()>;
	using SerializeNotification = std::move_only_function<void(std::exception_ptr)>;
#else
	using SerializeFunction = std::function<void()>;
	using SerializeNotification = std::function<void(std::exception_ptr)>;
#endif
	struct SerializeTask {
		SerializeFunction execute;
		SerializeNotification notify;
	};

	void ScheduleSerialize(SerializeTask&& task) {
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
							[]() {
								return !tasks.empty();
							}
						);

						// A stop request only terminates the thread after all tasks accepted
						// before shutdown have been drained.
						if (tasks.empty()) {
							return;
						}

						current = std::move(tasks.front());
						tasks.pop_front();
					}

					std::exception_ptr error;
					try {
						current.execute();
					}
					catch (std::exception const& ex) {
						error = std::current_exception();
						if (fyuu_asset::log::Warning) {
							fyuu_asset::log::Warning(
								std::format(
									"Some error occurred while serializing the asset: {}",
									ex.what()
								),
								std::source_location::current()
							);
						}
					}
					catch (...) {
						error = std::current_exception();
						if (fyuu_asset::log::Warning) {
							fyuu_asset::log::Warning(
								"An unknown error occurred while serializing the asset",
								std::source_location::current()
							);
						}
					}
					current.notify(std::move(error));
				}
			}
		);

		{
			std::lock_guard<std::mutex> lock(mutex);
			tasks.emplace_back(std::move(task));
		}
		condition.notify_one();
	}
}

namespace fyuu_asset::detail {
	void AsyncSerialize(SerializeFunction&& execute, SerializeNotification&& notify) {
		ScheduleSerialize(
			SerializeTask{ std::move(execute), std::move(notify) }
		);
	}

	template <class F, class N>
	void AsyncSerialize(F&& task, N&& notify) {
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
		AsyncSerialize(
			SerializeFunction{ std::forward<F>(task) },
			SerializeNotification{ std::forward<N>(notify) }
		);
#else
		using Function = std::decay_t<F>;
		using Notification = std::decay_t<N>;
		SerializeFunction function;
		SerializeNotification notification;
		if constexpr (std::is_copy_constructible_v<Function>) {
			function = std::forward<F>(task);
		}
		else {
			// C++20 std::function needs a copyable target. Only the fallback pays
			// for shared storage when the submitted callable itself is move-only.
			auto owned = std::make_shared<Function>(std::forward<F>(task));
			function = [owned = std::move(owned)]() mutable {
				std::invoke(*owned);
			};
		}
		if constexpr (std::is_copy_constructible_v<Notification>) {
			notification = std::forward<N>(notify);
		}
		else {
			auto owned = std::make_shared<Notification>(std::forward<N>(notify));
			notification = [owned = std::move(owned)](std::exception_ptr error) mutable {
				std::invoke(*owned, std::move(error));
			};
		}
		AsyncSerialize(
			std::move(function),
			std::move(notification)
		);
#endif
	}

}

namespace fyuu_asset {
	export namespace execution {
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
		UUID m_id;

		// T must die with the last strong reference, potentially before the Asset
		// allocation can be released. The union disables automatic destruction, so
		// intrusive_ptr_release() can destroy T exactly once. After that, only the
		// counters and Weak operations remain valid; Get() requires a live ManagedAsset.

		union {
			T m_data;
		};

		template <class... Args>
		explicit Asset(UUID const& id, Args&&... args)
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

		inline static std::unordered_map<UUID, Weak, UUIDHash, UUIDEquality> s_loaded_assets;
		inline static std::shared_mutex s_mutex;

		static void Unregister(Asset* asset) noexcept {
			std::unique_lock lock(s_mutex);
			auto found = s_loaded_assets.find(asset->m_id);
			if (found != s_loaded_assets.end() && found->second.m_asset == asset) {
				s_loaded_assets.erase(found);
			}
		}

		template <class... Args>
		[[nodiscard]] static ManagedAsset CreateWithID(UUID const& id, Args&&... args) {
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
				GenerateUUID(),
				std::forward<Args>(args)...
			);
		}

		[[nodiscard]] static ManagedAsset Find(UUID const& id) noexcept {
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

		[[nodiscard]] UUID GetID() const noexcept {
			return m_id;
		}

		[[nodiscard]] decltype(auto) Get(this auto&& self) noexcept {
			return (self.m_data);
		}

	private:
		friend void intrusive_ptr_add_ref(Asset* asset) noexcept {
			asset->m_strong.fetch_add(1, std::memory_order_relaxed);
		}

		template <class N>
		static void QueueSerialize(UUID const& id, T data, N&& notify) {
			detail::AsyncSerialize(
				[id, data = std::move(data)]() mutable {
					std::string structure_name;
#if defined(__cpp_lib_reflection)
					structure_name = std::meta::identifier_of(^^T);
#else
					structure_name = boost::typeindex::type_id<T>().pretty_name();
					if (auto position = structure_name.rfind("::"); position != std::string::npos) {
						structure_name.erase(0, position + 2);
					}
					if (auto position = structure_name.find('['); position != std::string::npos) {
						structure_name.erase(position);
					}
#endif
					std::erase_if(
						structure_name,
						[](char character) {
							return !((character >= 'a' && character <= 'z') ||
								(character >= 'A' && character <= 'Z') ||
								(character >= '0' && character <= '9') ||
								character == '_');
						}
					);
					auto path = GetPath(
						fs::path{ structure_name } / (UUIDToString(id) + ".json")
					);
					fs::create_directories(path.parent_path());
					if constexpr (requires { data.Serialize(path); }) {
						data.Serialize(path);
					} else {
						nlohmann::json serialized = nlohmann::json::object();
#if defined(__cpp_lib_reflection)
						constexpr auto context = std::meta::access_context::current();
						template for (constexpr auto member : std::define_static_array(
										  std::meta::nonstatic_data_members_of(^^T, context))) {
							if constexpr (std::meta::has_identifier(member)) {
								serialized[std::meta::identifier_of(member)] = data.[:member:];
							}
						}
#else
						using Members =
							boost::describe::describe_members<T, boost::describe::mod_public>;
						boost::mp11::mp_for_each<Members>(
							[&](auto member) {
								serialized[member.name] = data.*member.pointer;
							}
						);
#endif
						auto temporary = path;
						temporary += ".tmp";

						{
							std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
							if (!output) {
								throw std::runtime_error(
									std::format(
										"Failed to open asset file '{}'",
										temporary.string()
									)
								);
							}
							output << serialized.dump(2);
							output.flush();
							if (!output) {
								throw std::runtime_error(
									std::format(
										"Failed to write asset file '{}'",
										temporary.string()
									)
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
									"Failed to publish asset file '{}': {}", path.string(),
									error.message()
								)
							);
						}
					}
				},
				std::forward<N>(notify)
			);
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
			asset->m_data.~T();

			// Release the implicit weak reference owned by the former strong set. Real
			// Weak objects may continue to keep the allocation (but not T) alive.
			if (asset->m_weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				delete asset;
			}
		}

	public:
		[[nodiscard]] plastic::concurrency::SerialTask<void> Save() const
			requires std::copy_constructible<T> {
			struct SaveAwaiter {
				Asset const* asset;
				std::exception_ptr error;

				[[nodiscard]] bool await_ready() const noexcept {
					return false;
				}

				void await_suspend(std::coroutine_handle<> continuation) {
					Asset::QueueSerialize(
						asset->m_id,
						asset->m_data,
						[this, continuation](std::exception_ptr result) mutable noexcept {
							error = std::move(result);
							// This resumes Save() on the asset writer thread. Save() currently only
							// publishes its result and finishes; code awaiting Save() must schedule
							// itself back onto the required thread before doing thread-bound work.
							continuation.resume();
						}
					);
				}

				void await_resume() {
					if (error) {
						std::rethrow_exception(error);
					}
				}
			};

			// SaveAwaiter suspends this coroutine until serialization finishes. Its
			// continuation is resumed directly by the asset writer thread.
			co_await SaveAwaiter{
				.asset = this,
				.error = {}
			};
		}
	};

} // namespace fyuu_asset
