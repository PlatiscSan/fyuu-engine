module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <filesystem>
#include <fstream>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <stop_token>
#include <thread>
#include <utility>
#include <concepts>
#include <compare>
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
export module fyuu_asset:asset_manager;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.serial_task;
import :asset;
import :base_asset;

namespace {
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
					value.[:member:] = source.at(
						std::meta::identifier_of(member)).template get<Member>();
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
				value.*member.pointer = source.at(member.name).template get<Member>();
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
				for (;;) {
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
			std::shared_ptr<Receiver> receiver;
			try {
				receiver = std::make_shared<Receiver>(std::move(m_receiver));
				ScheduleAssetTask(AssetTask{ [receiver](std::stop_token token) mutable noexcept {
					auto stopped = token.stop_requested();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
					if constexpr (requires {
						std::execution::get_stop_token(std::execution::get_env(*receiver));
					}) {
						stopped = stopped || std::execution::get_stop_token(
							std::execution::get_env(*receiver)).stop_requested();
					}
#else
					if constexpr (requires { receiver->get_env().get_stop_token(); }) {
						stopped = stopped || receiver->get_env().get_stop_token().stop_requested();
					}
#endif
					if (stopped) {
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
						std::execution::set_stopped(std::move(*receiver));
#else
						std::move(*receiver).set_stopped();
#endif
						return;
					}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
					std::execution::set_value(std::move(*receiver));
#else
					std::move(*receiver).set_value();
#endif
				} });
			}
			catch (...) {
				auto error = std::current_exception();
				if (receiver) {
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
					std::execution::set_error(std::move(*receiver), error);
#else
					std::move(*receiver).set_error(error);
#endif
				}
				else {
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
					std::execution::set_error(std::move(m_receiver), error);
#else
					std::move(m_receiver).set_error(error);
#endif
				}
			}
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
#endif

			auto path = GetPath(
				std::filesystem::path{ "conf" } / structure_name / (boost::uuids::to_string(id) + ".json")
			);
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				throw std::runtime_error(std::format("Failed to open asset file '{}'", path.string()));
			}

			nlohmann::json document;
			input >> document;
			return Asset<T>::CreateWithID(id, DeserializeAssetValue<T>(document));
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
