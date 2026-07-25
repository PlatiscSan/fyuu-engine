// ============================================================================
// circular_buffer.cppm - Module interface for a concurrent circular buffer
// ============================================================================
//
// This module provides a thread‑safe bounded circular (ring) buffer that
// supports multiple producers and a single consumer (or multiple consumers if
// they synchronize externally). It uses atomic operations for lock‑free
// operation and a bitmap to indicate when a slot is fully constructed,
// preventing the consumer from reading incompletely written elements.

module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <optional>
#include <atomic>
#include <array>
#include <span>
#include <algorithm>
#include <utility>
#include <cstddef>
#include <new>
#endif // !defined(__cpp_lib_modules)
export module plastic.circular_buffer;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.bitmap;
namespace plastic::concurrency {

	// ------------------------------------------------------------------------
	// CircularBuffer – lock‑safe bounded circular buffer for multiple producers
	// ------------------------------------------------------------------------

	/**
	 * @brief A thread‑safe circular buffer with a fixed capacity.
	 *
	 * This buffer is implemented as an array of optional<T> slots, plus two
	 * atomic indices (read and write) and a bitmap. The bitmap marks slots
	 * where the element has been fully constructed, allowing the consumer to
	 * wait until construction is complete before reading.
	 *
	 * The buffer supports:
	 *   - Multiple concurrent producers (via emplace_back/push_back).
	 *   - A single consumer (pop_front) – if multiple consumers are needed,
	 *	 they must synchronize externally.
	 *   - Wait‑free push (except when full) and a blocking pop option.
	 *
	 * @tparam T		The type of elements stored in the buffer.
	 * @tparam Capacity The maximum number of elements the buffer can hold.
	 *				  Must be > 0 and less than the maximum bits of the
	 *				  underlying bitmap (typically 64 for size_t).
	 */
	export template <class T, std::size_t Capacity>
	class CircularBuffer {
	public:
		// Standard container type aliases.
		using size_type = std::size_t;
		using value_type = T;
		using reference = T&;
		using const_reference = T const&;
		using pointer = T*;
		using const_pointer = T const*;

		// The bitmap uses the same size_type, with cache‑line alignment to avoid false sharing.
		using Bitmap = Bitmap<size_type, true>;

		static_assert(Capacity > size_type(0), "Capacity must be greater than 0");
		static_assert(Capacity < Bitmap::MaxBits(), "Capacity out of range for the bitmap");

	private:
		// We allocate one extra slot to distinguish full from empty.
		static constexpr size_type SlotCount = Capacity + 1;
		std::array<std::optional<T>, SlotCount> m_slots;

		// Read and write indices are placed on separate cache lines to avoid false sharing.
		alignas(std::hardware_constructive_interference_size) std::atomic<size_type> m_read_pos;
		alignas(std::hardware_constructive_interference_size) std::atomic<size_type> m_write_pos;

		// Bitmap indicating which slots contain fully constructed objects.
		Bitmap m_bitmap;

	public:
		// --------------------------------------------------------------------
		// Construction / destruction
		// --------------------------------------------------------------------

		/// Default constructor – creates an empty buffer.
		CircularBuffer() noexcept = default;

		/**
		 * @brief Constructs the buffer and fills it with elements from a span.
		 * @param view A span of elements to copy into the buffer. At most Capacity
		 *			 elements are taken.
		 */
		CircularBuffer(std::span<T const> view) {
			size_type length = std::min(view.size(), Capacity);
			for (size_type i = 0; i < length; ++i) {
				emplace_back(view[i]);
			}
		}

		/// Destructor – destroys all remaining elements.
		~CircularBuffer() noexcept {
			while (!empty()) {
				(void)pop_front();
			}
		}

		// --------------------------------------------------------------------
		// Capacity observers
		// --------------------------------------------------------------------

		/// Returns true if the buffer is empty (read position equals write position).
		bool empty() const noexcept {
			return m_read_pos.load(std::memory_order::relaxed) == m_write_pos.load(std::memory_order::relaxed);
		}
		
		/// Returns the number of elements currently in the buffer.
		size_type size() const noexcept {
			// (write - read) modulo SlotCount.
			return (m_write_pos.load(std::memory_order::relaxed) - m_read_pos.load(std::memory_order::relaxed) + SlotCount) % SlotCount;
		}

		/// Returns the maximum number of elements the buffer can hold.
		size_type capacity() const noexcept {
			return Capacity;
		}

		// --------------------------------------------------------------------
		// Producer operations (push)
		// --------------------------------------------------------------------

		/**
		 * @brief Constructs an element in‑place at the back of the buffer.
		 *
		 * This operation is lock‑free. If the buffer is full, it returns false
		 * immediately. Otherwise, it atomically reserves a slot, constructs the
		 * element, and marks the slot as ready.
		 *
		 * @tparam Args Constructor argument types.
		 * @param args Arguments forwarded to T's constructor.
		 * @return true if the element was successfully added, false if the buffer is full.
		 */
		template <class... Args>
		bool emplace_back(Args&&... args) {
			size_type expected_pos = m_write_pos.load(std::memory_order::acquire);
			size_type desired_pos;

			do {
				desired_pos = (expected_pos + 1) % SlotCount;
				// Full if the next write position would equal the read position.
				if (desired_pos == m_read_pos.load(std::memory_order::acquire)) {
					return false;   // buffer full
				}
			} while (!m_write_pos.compare_exchange_weak(
				expected_pos,
				desired_pos,
				std::memory_order::release,
				std::memory_order::relaxed));

			// Notify any waiting consumer that the write position has moved.
			m_write_pos.notify_all();

			// Construct the element in the reserved slot.
			m_slots[expected_pos].emplace(std::forward<Args>(args)...);
			// Mark the slot as ready for the consumer.
			(void)m_bitmap.TestAndSet(expected_pos);

			return true;
		}

		/// Copies a value into the buffer (equivalent to emplace_back(val)).
		bool push_back(value_type const& val) {
			return emplace_back(val);
		}

		/// Moves a value into the buffer (equivalent to emplace_back(std::move(val))).
		bool push_back(value_type&& val) {
			return emplace_back(std::move(val));
		}

		// --------------------------------------------------------------------
		// Consumer operations (pop)
		// --------------------------------------------------------------------

		/**
		 * @brief Removes and returns the front element, or returns std::nullopt if empty.
		 *
		 * If `wait_for_element` is true and the buffer is empty, the call blocks
		 * until an element becomes available. The waiting is efficient (uses
		 * atomic::wait) and does not spin.
		 *
		 * @param wait_for_element If true, blocks until an element is present.
		 * @return std::optional<T> – the popped element, or std::nullopt if
		 *		 the buffer is empty and waiting was not requested.
		 */
		std::optional<value_type> pop_front(bool wait_for_element = false) {
			size_type expected_pos = m_read_pos.load(std::memory_order::acquire);
			size_type desired_pos;
			bool cas_done = false;

			do {
				// Check if the buffer is empty.
				if (expected_pos == m_write_pos.load(std::memory_order::acquire)) {
					if (wait_for_element) {
						// Block until the write position changes.
						m_write_pos.wait(expected_pos, std::memory_order::relaxed);
						// Reload expected_pos after waking.
						expected_pos = m_read_pos.load(std::memory_order::acquire);
						continue;
					}
					return std::nullopt;
				}

				// If the slot is not yet marked as ready (producer still constructing),
				// we must wait. The bitmap test is not atomic with the read position,
				// but this is safe because the producer sets the bitmap only after
				// construction and before any subsequent write.
				if (!m_bitmap.Test(expected_pos)) {
					// The element is still being constructed; yield or continue.
					// A simple spin is used here because the window is very short.
					continue;
				}

				desired_pos = (expected_pos + 1) % SlotCount;

				cas_done = m_read_pos.compare_exchange_weak(
					expected_pos,
					desired_pos,
					std::memory_order::release,
					std::memory_order::relaxed
				);
				// If CAS fails, expected_pos is updated with the current value,
				// and the loop repeats.
			} while (!cas_done);

			// Successfully reserved the slot for reading. Move the element out.
			std::optional<value_type> popped = std::move(m_slots[expected_pos]);
			// Clear the slot and the bitmap (optional reset).
			m_bitmap.Clear(expected_pos);

			return popped;
		}
	};

} // namespace plastic::concurrency