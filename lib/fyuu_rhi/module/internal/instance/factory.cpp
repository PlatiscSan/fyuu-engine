module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)
module fyuu_rhi:instance_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;
import :instance;

namespace fyuu_rhi {

	struct InstanceImplementation {
		Backend type;
		void* native;
	};

#if defined(_MSC_VER)
	[[msvc::noinline]]
#endif // defined(_MSC_VER)
	void ThrowInstanceBackendNotImplemented() {
		throw std::runtime_error("This backend is not implemented yet");
	}

	template <class Instance> 
	struct CreateInstance {
		Instance operator()() const {
			ThrowInstanceBackendNotImplemented();
			return {};
		}
	};

}
