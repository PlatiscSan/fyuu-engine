module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <type_traits>
#include <exception>
#include <memory>
#include <variant>
#include <coroutine>
#include <concepts>
#include <utility>
#endif // !defined(__cpp_lib_modules)
export module plastic.serial_task;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace plastic::concurrency {

	namespace detail {

		// Success-state marker for SerialTask<void>.
		struct DoneTag {};

		// Variant alternative that carries the coroutine result; maps void -> DoneTag so
		// a single State layout works for every T.
		template <class Storage>
		using StorageType = std::conditional_t<std::is_void_v<Storage>, DoneTag, Storage>;

		// Shared, reference-counted task state.
		//
		// Ownership model: every SerialTask copy holds a shared_ptr to a State; the
		// coroutine frame's promise holds only a *borrowed* raw pointer to it (used while
		// the coroutine executes, and never past it). The State owns the frame while the
		// coroutine is suspended: ~SerialTaskState() destroys it via the stored handle.
		// Because the promise is not a counted owner, the State — and hence a suspended
		// frame — is released the moment the last task copy is dropped.
		//
		// On completion the result is copied into State (return_value/return_void/
		// unhandled_exception), then final_suspend() (suspend_never) lets the machinery
		// destroy the frame immediately; the State — with the result — survives.
		template <class Storage>
		struct SerialTaskState {
			// The frame this state owns while it is suspended; dangles once the coroutine
			// completes and the machinery destroys the frame (see PromiseBase::final_suspend).
			std::coroutine_handle<> handle;
			// False once the coroutine completed and the machinery destroyed the frame;
			// cleared in final_suspend so ~SerialTaskState skips a second destroy.
			bool m_frame_alive = true;
			// 0 = Running (monostate), 1 = Succeeded (StorageType<Storage>), 2 = Failed.
			std::variant<std::monostate, StorageType<Storage>, std::exception_ptr> m_result;
			// True while handle.resume() is on the stack, guarding against re-entrant
			// resumes of the same frame.
			bool resuming = false;

			~SerialTaskState() {
				if (resuming) {
					// The last task reference was dropped while the coroutine is executing;
					// destroying the frame underneath it would be undefined behavior.
					std::terminate();
				}
				if (m_frame_alive && handle) {
					// Coroutine still suspended (not completed): destroy the frame so nothing
					// leaks. A completed coroutine's frame is already gone (final_suspend).
					handle.destroy();
				}
			}
		};

		// Promise scaffolding shared by SerialTask<T> and SerialTask<void>. Only the
		// concrete return_value()/return_void() lives in the derived promise type. The
		// promise stores results through a borrowed pointer to the shared State; all
		// counted ownership sits in the task objects.
		template <class Storage, class Task>
		struct PromiseBase {
			SerialTaskState<Storage>* state = nullptr;

			Task get_return_object() {
				auto shared = std::make_shared<SerialTaskState<Storage>>();
				auto& promise = static_cast<Task::promise_type&>(*this);
				shared->handle = std::coroutine_handle<Task::promise_type>::from_promise(promise);
				state = shared.get();
				return Task{shared};
			}

			static constexpr std::suspend_never initial_suspend() noexcept {
				return {};
			}

			// suspend_never: the machinery destroys the frame right after completion, so
			// the result is copied into State first. The frame-alive flag is cleared here
			// to keep ~SerialTaskState from destroying the already-gone frame.
			std::suspend_never final_suspend() noexcept {
				state->m_frame_alive = false;
				return {};
			}

			void unhandled_exception() {
				state->m_result.template emplace<std::exception_ptr>(std::current_exception());
			}
		};

		// Query/advance logic common to the value and void tasks.
		template <class Storage>
		class SerialTaskBase {
		public:
			using State = SerialTaskState<Storage>;
		protected:
			std::shared_ptr<State> m_state;

		public:
			explicit SerialTaskBase(std::shared_ptr<State> const& state) noexcept
				: m_state(state) {
			}

			// True once the coroutine reached a terminal state (succeeded or failed).
			bool IsDone() const noexcept {
				return m_state->m_result.index() != 0;
			}

			// True if the coroutine terminated by throwing.
			bool HasException() const noexcept {
				return m_state->m_result.index() == 2;
			}

			// Advances the coroutine one step. No-op once succeeded; rethrows the stored
			// exception once failed. Must not be called while the coroutine is executing
			// (i.e. re-entrantly from inside the coroutine body).
			void Resume() {
				switch (m_state->m_result.index()) {
					case 0: { // Running
						if (m_state->resuming) {
							// Resuming the same frame while it is already executing is UB.
							std::terminate();
						}
						m_state->resuming = true;
						m_state->handle.resume();
						m_state->resuming = false;
						break;
					}
					case 1: // Succeeded
						break;
					default: // Failed
						std::rethrow_exception(std::get<std::exception_ptr>(m_state->m_result));
				}
			}

		};

	} // namespace detail

	export template <class T> class SerialTask : public detail::SerialTaskBase<T> {
		static_assert(!std::is_void_v<T> && !std::is_reference_v<T>
			&& !std::same_as<T, std::exception_ptr>,
			"SerialTask<T> requires a non-void, non-reference value type other than std::exception_ptr");

	public:
		struct promise_type : public detail::PromiseBase<T, SerialTask> {
			template <std::convertible_to<T> U>
			void return_value(U&& val) {
				this->state->m_result.template emplace<T>(std::forward<U>(val));
			}
		};

		using Base = detail::SerialTaskBase<T>;
		using Base::Base;

		// Pointer to the stored result; nullptr while running; rethrows the stored
		// exception once failed. The pointee's constness follows the task's constness.
		template <class Self>
		auto Result(this Self&& self)
			-> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, T const*, T*> {
			auto* value = std::get_if<T>(&self.m_state->m_result);
			if (value) {
				return value;
			}
			if (auto* exc = std::get_if<std::exception_ptr>(&self.m_state->m_result); exc) {
				std::rethrow_exception(*exc);
			}
			return nullptr;
		}
	};

	export template <> class SerialTask<void> : public detail::SerialTaskBase<void> {
	public:
		struct promise_type : detail::PromiseBase<void, SerialTask<void>> {
			void return_void() {
				this->state->m_result.template emplace<detail::DoneTag>();
			}
		};

		using Base = detail::SerialTaskBase<void>;
		using Base::Base;
	};

} // namespace plastic::concurrency
