module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:execution_pool;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_rhi::execution {

	template <class Value> class ExecutionPool {
	private:
		struct Slot {
			Value value;
			bool acquired = false;
		};

		struct State {
			std::deque<Slot> slots;
			std::mutex mutex;
		};

		static void NoReset(Value&) noexcept {

		}

		std::shared_ptr<State> m_state = std::make_shared<State>();

	public:
		class Lease {
		private:
			std::shared_ptr<State> m_state;
			Slot* m_slot = nullptr;

			void Release() noexcept {
				if (!m_slot) {
					return;
				}
				std::unique_lock<std::mutex> lock(m_state->mutex);
				m_slot->acquired = false;
				m_slot = nullptr;
			}

		public:
			Lease(std::shared_ptr<State> const& state, Slot* slot) noexcept
				: m_state(state), m_slot(slot) {

			}

			Lease(Lease const&) = delete;
			Lease& operator=(Lease const&) = delete;
			Lease(Lease&& other) noexcept
				: m_state(other.m_state),
				m_slot(std::exchange(other.m_slot, nullptr)) {

			}
			Lease& operator=(Lease&& other) noexcept {
				Release();
				m_state = other.m_state;
				m_slot = std::exchange(other.m_slot, nullptr);
				return *this;
			}
			~Lease() noexcept {
				Release();
			}

			[[nodiscard]] Value& Get() noexcept {
				return m_slot->value;
			}

			[[nodiscard]] Value const& Get() const noexcept {
				return m_slot->value;
			}
	};

		template <class Create, class Reset>
		[[nodiscard]] Lease Acquire(Create const& create, Reset const& reset) {
			std::unique_lock<std::mutex> lock(m_state->mutex);
			for (auto& slot : m_state->slots) {
				if (!slot.acquired) {
					reset(slot.value);
					slot.acquired = true;
					return Lease(m_state, &slot);
				}
			}

			m_state->slots.push_back({
				.value = create(),
				.acquired = true
			});
			return Lease(m_state, &m_state->slots.back());
		}

		template <class Create>
		[[nodiscard]] Lease Acquire(Create const& create) {
			return Acquire(create, NoReset);
		}
	};

}
