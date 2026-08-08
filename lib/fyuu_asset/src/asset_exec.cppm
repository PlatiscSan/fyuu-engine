module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <utility>

#include <memory>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <deque>
#include <vector>

#include <exception>
#include <stdexcept>

#include <functional>
#include <future>

#include <fstream>
#include <string>

#include <filesystem>

#include <concepts>
#include <compare>
#include <stop_token>

#if defined(__cpp_lib_reflection)
#include <meta>
#endif // defined(__cpp_lib_reflection)
#endif // !defined(__cpp_lib_modules)
#include <boost/type_index.hpp>
#include <boost/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#if !defined(__cpp_lib_reflection)
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#endif // !defined(__cpp_lib_reflection)
#include <nlohmann/json.hpp>
export module fyuu_asset:asset_exec;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.serial_task;
import :asset;
import :base_asset;

namespace fyuu_asset::execution::detail {
	template <class T>
	T DeserializeAssetValue(nlohmann::json const& source) {
		if constexpr (requires {
			{ T::Deserialize(source) } -> std::same_as<T>;
		}) {
			return T::Deserialize(source);
		}
#if defined(__cpp_lib_reflection)
		else {
			static_assert(std::is_default_constructible_v<T>,
				"T must be default constructible or provide T::Deserialize(json)");
			T value{};
			constexpr auto context = std::meta::access_context::current();
			template for (constexpr auto member : std::define_static_array(
				std::meta::nonstatic_data_members_of(^^T, context))) {
				if constexpr (std::meta::has_identifier(member)) {
					using Member = std::remove_cvref_t<decltype(value.[:member:])>;
					if constexpr (std::same_as<Member, std::vector<std::byte>>) {
						for (auto const& byte : source.at(std::meta::identifier_of(member))) {
							value.[:member:].push_back(
								static_cast<std::byte>(byte.template get<unsigned int>())
							);
						}
					}
					else {
						value.[:member:] = source.at(
							std::meta::identifier_of(member)).template get<Member>();
					}
				}
			}
			return value;
		}
#else
		else if constexpr (boost::describe::has_describe_members<T>::value) {
			static_assert(std::is_default_constructible_v<T>,
				"T must be default constructible or provide T::Deserialize(json)");
			T value{};
			using Members = boost::describe::describe_members<T, boost::describe::mod_public>;
			boost::mp11::mp_for_each<Members>([&](auto member) {
				using Member = std::remove_cvref_t<decltype(value.*member.pointer)>;
				if constexpr (std::same_as<Member, std::vector<std::byte>>) {
					for (auto const& byte : source.at(member.name)) {
						(value.*member.pointer).push_back(
							static_cast<std::byte>(byte.template get<unsigned int>())
						);
					}
				}
				else {
					value.*member.pointer = source.at(member.name).template get<Member>();
				}
			});
			return value;
		}
		else {
			return source.template get<T>();
		}
#endif
	}

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
	using AssetTask = std::move_only_function<void(std::stop_token)>;
#else
	using AssetTask = std::packaged_task<void(std::stop_token)>;
#endif

	void ScheduleAssetTask(AssetTask&& submitted) {
		using TaskQueue = std::deque<AssetTask>;

		static TaskQueue tasks;
		static std::mutex mutex;
		static std::condition_variable_any condition;
		static std::jthread worker(
			[](std::stop_token token) noexcept {
				while (!token.stop_requested()) {
					AssetTask task;
					{
						std::unique_lock lock(mutex);
						condition.wait(lock, token, [&] { return !tasks.empty(); });
						if (tasks.empty()) {
							return;
						}
						task = std::move(tasks.front());
						tasks.pop_front();
					}
					task(token);
				}
			}
		);

		{
			std::lock_guard lock(mutex);
			tasks.emplace_back(std::move(submitted));
		}
		condition.notify_one();
	}
}

namespace fyuu_asset::execution {

	/// Operation state returned by AssetLoader::connect(). start() runs
	/// the load on a worker thread and completes the receiver with set_value /
	/// set_error / set_stopped.
	export template <class Receiver> class AssetLoadOperation {
private:
		friend class AssetLoader;
		Receiver m_receiver;

		explicit AssetLoadOperation(Receiver&& receiver) noexcept
			: m_receiver(std::move(receiver)) {
		}

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif

		void start() noexcept {
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			detail::AssetTask task = [receiver = std::move(m_receiver)](
				std::stop_token token
			) mutable noexcept {
				auto stopped = token.stop_requested();
				if constexpr (requires {
					std::execution::get_stop_token(std::execution::get_env(receiver));
				}) {
					stopped = stopped || std::execution::get_stop_token(
						std::execution::get_env(receiver)).stop_requested();
				}
				if (stopped) {
					std::execution::set_stopped(std::move(receiver));
					return;
				}
				std::execution::set_value(std::move(receiver));
			};
#else
			detail::AssetTask task = [receiver = std::move(m_receiver)](
				std::stop_token token
			) mutable noexcept {
				auto stopped = token.stop_requested();
				if constexpr (requires { receiver.get_env().get_stop_token(); }) {
					stopped = stopped || receiver.get_env().get_stop_token().stop_requested();
				}
				if (stopped) {
					std::move(receiver).set_stopped();
					return;
				}
				std::move(receiver).set_value();
			};
#endif
			detail::ScheduleAssetTask(std::move(task));
		}


	};

	/// Sender produced by AssetScheduler::schedule().
	export class AssetLoader {
private:
		friend class AssetScheduler;

	public:
		AssetLoader() = default;

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
		using completion_signatures = std::execution::completion_signatures<
			std::execution::set_value_t(),
			std::execution::set_error_t(std::exception_ptr),
			std::execution::set_stopped_t()
		>;
#endif
		template <class T>
		[[nodiscard]] typename Asset<T>::ManagedAsset Load(boost::uuids::uuid const& id) const {
			if (auto loaded = Asset<T>::Find(id)) {
				return loaded;
			}

			std::string structure_name;
#if defined(__cpp_lib_reflection)
			structure_name = std::meta::identifier_of(^^T);
#else
			structure_name = boost::typeindex::type_id<T>().pretty_name();
			if (auto position = structure_name.rfind("::");
				position != std::string::npos) {
				structure_name.erase(0, position + 2);
			}
			if (auto position = structure_name.find('[');
				position != std::string::npos) {
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
				std::filesystem::path{ structure_name } / (boost::uuids::to_string(id) + ".json")
			);
			if constexpr (requires {
				{ T::Deserialize(path) } -> std::same_as<T>;
			}) {
				return Asset<T>::CreateWithID(id, T::Deserialize(path));
			}
			else {
				std::ifstream input(path, std::ios::binary);
				if (!input) {
					throw std::runtime_error(std::format("Failed to open asset file '{}'", path.string()));
				}

				nlohmann::json document;
				document = nlohmann::json::parse(input);
				return Asset<T>::CreateWithID(id, detail::DeserializeAssetValue<T>(document));
			}
		}

		template <class Receiver>
		[[nodiscard]] AssetLoadOperation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && noexcept {
			return AssetLoadOperation<std::remove_cvref_t<Receiver>>{
				std::forward<Receiver>(receiver)
			};
		}
	};

	/// P2300 std::execution::scheduler for asset loads: copyable,
	/// equality-comparable, schedule() returns a sender that runs the load
	/// on a background worker thread.
	export class AssetScheduler {
	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using scheduler_concept = std::execution::scheduler_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		AssetScheduler() = default;

		[[nodiscard]] AssetLoader schedule() const noexcept {
			return AssetLoader{};
		}

		std::strong_ordering operator<=>(AssetScheduler const&) const noexcept = default;


	};


} // namespace fyuu_asset::execution
