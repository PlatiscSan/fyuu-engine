#include <cstddef>
#include <tbb/parallel_for.h>

extern "C" void ParallelFor(
	std::size_t first,
	std::size_t last,
	void* function,
	void (*invoke)(void*, std::size_t)
) {
	if (last <= first) {
		return;
	}
	// A command graph usually owns only a handful of batches, and one batch is far too
	// coarse for TBB to split further. Deriving tasks for one or two of them costs more
	// than it hides, so those ranges run inline on the calling thread.
	if (last - first == 1u) {
		invoke(function, first);
		return;
	}
	tbb::parallel_for(
		first,
		last,
		[function, invoke](std::size_t index) {
			invoke(function, index);
		}
	);
}
