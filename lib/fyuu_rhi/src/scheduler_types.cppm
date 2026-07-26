module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <exception>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:scheduler_types;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.atomic_flags;

namespace fyuu_rhi::execution {
	export enum class SchedulerFlagBits : std::uint8_t {
		Graphics,
		Compute,
		Copy,
		Count
	};

	export using SchedulerFlags = plastic::concurrency::AtomicFlags<SchedulerFlagBits>;

	export struct SchedulerDescriptor {
		SchedulerFlags flags;
		float priority = 0.5f;
	};

	export struct SchedulerCompletion {
		void* operation;
		void (*SetValue)(void*) noexcept;
		void (*SetError)(void*, std::exception_ptr const&) noexcept;
		void (*SetStopped)(void*) noexcept;
	};

	struct DeferredDestroy {
		void* object = nullptr;
		void (*Destroy)(void*) noexcept = nullptr;
		SchedulerCompletion completion;
	};

	struct ResourceMapCompletion {
		void* operation = nullptr;
		void (*SetValue)(void*, std::byte*) noexcept = nullptr;
		void (*SetError)(void*, std::exception_ptr const&) noexcept = nullptr;
	};

	struct ResourceMapRequest {
		std::size_t offset = 0u;
		std::size_t size = 0u;
		bool read = false;
		bool write = false;
		ResourceMapCompletion completion;
	};

	struct ResourceUnmapRequest {
		std::size_t offset = 0u;
		std::size_t size = 0u;
		bool write = false;
		SchedulerCompletion completion;
	};

}
