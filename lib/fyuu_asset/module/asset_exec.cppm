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
export module fyuu_asset:asset_exec;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.serial_task;
import :asset;
import :base_asset;
import :uuid;

namespace fyuu_asset::execution::detail {
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
		static std::jthread worker([](std::stop_token token) noexcept {
			while (!token.stop_requested()) {
				AssetTask task;
				{
					std::unique_lock lock(mutex);
					condition.wait(lock, token, [&] {
						return !tasks.empty();
					});
					if (tasks.empty()) {
						return;
					}
					task = std::move(tasks.front());
					tasks.pop_front();
				}
				task(token);
			}
		});

		{
			std::lock_guard lock(mutex);
			tasks.emplace_back(std::move(submitted));
		}
		condition.notify_one();
	}
} // namespace fyuu_asset::execution::detail

namespace fyuu_asset::execution {

	/// Operation state returned by AssetLoader::connect(). start() runs
	/// the load on a worker thread and completes the receiver with set_value /
	/// set_error / set_stopped.
	export template <class Receiver> class AssetLoadOperation {
	private:
		friend class AssetLoader;
		Receiver m_receiver;

		explicit AssetLoadOperation(Receiver&& receiver) noexcept :
		    m_receiver(std::move(receiver)) {
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
					stopped = stopped ||
					    std::execution::get_stop_token(std::execution::get_env(receiver))
					        .stop_requested();
				}
				if (stopped) {
					std::execution::set_stopped(std::move(receiver));
					return;
				}
				std::execution::set_value(std::move(receiver));
			};
#else
			detail::AssetTask task =
			    [receiver = std::move(m_receiver)](std::stop_token token) mutable noexcept {
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
		    std::execution::set_stopped_t()>;
#endif
		template <class T>
		[[nodiscard]] typename Asset<T>::ManagedAsset Load(UUID const& id) const {
			if (auto loaded = Asset<T>::Find(id)) {
				return loaded;
			}

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
			std::erase_if(structure_name, [](char character) {
				return !(
				    (character >= 'a' && character <= 'z') ||
				    (character >= 'A' && character <= 'Z') ||
				    (character >= '0' && character <= '9') || character == '_'
				);
			});

			auto path = GetPath(std::filesystem::path{structure_name} / (id.ToString() + ".json"));
			return Asset<T>::CreateWithID(id, T::Deserialize(path));
		}

		template <class Receiver>
		[[nodiscard]] AssetLoadOperation<std::remove_cvref_t<Receiver>> connect(
		    Receiver&& receiver
		) && noexcept {
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
