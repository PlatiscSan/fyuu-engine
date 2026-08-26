#include <version>
#include <cstddef>
#include <tbb/parallel_for.h>

extern "C" void ParallelFor(
	std::size_t first,
	std::size_t last,
	void* function,
	void (*invoke)(void*, std::size_t)
) {
	tbb::parallel_for(
		first,
		last,
		[function, invoke](std::size_t index) {
			invoke(function, index);
		}
	);
}
