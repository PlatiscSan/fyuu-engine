module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
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

}
