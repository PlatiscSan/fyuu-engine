module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <type_traits>
#include <exception>
#include <memory>
#include <variant>
#include <coroutine>
#include <concepts>
#endif // !defined(__cpp_lib_modules)
export module plastic.serial_task;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace plastic::concurrency {

	export template <class T> class SerialTask {
	public:
		struct Context {
			std::coroutine_handle<> handle;
			std::variant<std::monostate, T, std::exception_ptr> state;
		};

		struct promise_type {
			std::shared_ptr<Context> context;

			SerialTask get_return_object() {
				if (!context) {
					context = std::make_shared<Context>(std::coroutine_handle<promise_type>::from_promise(*this), std::monostate{});
				}
				return SerialTask(context);
			}

			static constexpr std::suspend_never initial_suspend() noexcept {
				return {};
			}

			std::suspend_never final_suspend() noexcept {
				return {};
			}

			template <std::convertible_to<T> U>
			void return_value(U&& val) {
				context->state.template emplace<T>(std::forward<U>(val));
			}

			void unhandled_exception() {
				context->state.template emplace<std::exception_ptr>(std::current_exception());
			}
		};

	private:
		std::shared_ptr<Context> m_context;

	public:
		SerialTask(std::shared_ptr<Context> const& context) noexcept
			: m_context(context) {

		}

		template <class Self>
		auto Result(this Self&& self) 
			-> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, T const*, T*> {
			return std::visit(
				[](auto&& handle) -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, T const*, T*> {
					using Handle = std::decay_t<decltype(handle)>;
					if constexpr (std::same_as<Handle, T>) {
						return &handle;
					}
					else if constexpr (std::same_as<Handle, std::exception_ptr>) {
						std::rethrow_exception(handle);
					}
					else {
						return nullptr;
					}
				},
				self.m_context->state
			);
		} 

		template <class Self>
		bool IsDone(this Self&& self) {
			return std::visit(
				[](auto&& handle) {
					using Handle = std::decay_t<decltype(handle)>;
					if constexpr (std::same_as<Handle, T>) {
						return true;
					}
					else {
						return false;
					}
				},
				self.m_context->state
			);
		}

		void Resume() {
			return std::visit(
				[this](auto&& handle) {
					using Handle = std::decay_t<decltype(handle)>;
					if constexpr (std::same_as<Handle, T>) {
						
					}
					else if constexpr (std::same_as<Handle, std::exception_ptr>) {
						std::rethrow_exception(handle);
					}
					else {
						m_context->handle.resume();
					}
				},
				m_context->state
			);
		}

	};

	template <> class SerialTask<void> {
	public:
		struct DoneTag {};
		struct Context {
			std::coroutine_handle<> handle;
			std::variant<std::monostate, DoneTag, std::exception_ptr> state;
		};

		struct promise_type {
			std::shared_ptr<Context> context;

			SerialTask get_return_object() {
				if (!context) {
					context = std::make_shared<Context>(std::coroutine_handle<promise_type>::from_promise(*this), std::monostate{});
				}
				return SerialTask(context);
			}

			static constexpr std::suspend_never initial_suspend() noexcept {
				return {};
			}

			std::suspend_never final_suspend() noexcept {
				return {};
			}

			void return_void() {
				context->state.emplace<DoneTag>();
			}

			void unhandled_exception() {
				context->state.emplace<std::exception_ptr>(std::current_exception());
			}
		};

	private:
		std::shared_ptr<Context> m_context;

	public:
		SerialTask(std::shared_ptr<Context> const& context) noexcept
			: m_context(context) {

		}

		template <class Self>
		bool IsDone(this Self&& self) {
			return std::visit(
				[](auto&& handle) {
					using Handle = std::decay_t<decltype(handle)>;
					if constexpr (std::same_as<Handle, DoneTag>) {
						return true;
					}
					else {
						return false;
					}
				},
				self.m_context->state
			);
		}

		void Resume() {
			return std::visit(
				[this](auto&& handle) {
					using Handle = std::decay_t<decltype(handle)>;
					if constexpr (std::same_as<Handle, DoneTag>) {
						
					}
					else if constexpr (std::same_as<Handle, std::exception_ptr>) {
						std::rethrow_exception(handle);
					}
					else {
						m_context->handle.resume();
					}
				},
				m_context->state
			);
		}

	};

}