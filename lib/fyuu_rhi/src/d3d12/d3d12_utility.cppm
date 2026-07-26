/* d3d12_utility.cppm */
module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <system_error>
#include <type_traits>
#include <memory>
#include <vector>
#include <memory_resource>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <d3d12.h>
#include <comdef.h>
#include <wil/resource.h>
#endif // defined(_WIN32)
#define BOOST_DISABLE_ASSERTS
#include <boost/locale.hpp>
module fyuu_rhi:d3d12_utility;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {
	thread_local std::vector<wil::unique_event> s_events;
}

namespace fyuu_rhi::d3d12 {

	struct ManagedEvent {
		wil::unique_event impl;

		explicit ManagedEvent(wil::unique_event&& event) noexcept
			: impl(std::move(event)) {

		}

		ManagedEvent(ManagedEvent const&) = delete;
		ManagedEvent& operator=(ManagedEvent const&) = delete;
		ManagedEvent(ManagedEvent&&) noexcept = default;
		ManagedEvent& operator=(ManagedEvent&&) noexcept = default;

		~ManagedEvent() noexcept {
			if (impl) {
				s_events.emplace_back(std::move(impl));
			}
		}
	};

	void ThrowIfFailed(HRESULT result) {

		if (!FAILED(result)) {
			return;
		}

		_com_error error(result);

		std::string message = boost::locale::conv::utf_to_utf<char>(error.ErrorMessage());

		throw std::system_error(std::error_code(result, std::system_category()), message);
		
	}


	ManagedEvent CreateManagedEvent() {
		if (s_events.empty()) {
			return ManagedEvent(wil::unique_event(wil::EventOptions::None));
		}

		wil::unique_event event = std::move(s_events.back());
		s_events.pop_back();
		event.ResetEvent();

		return ManagedEvent(std::move(event));

	}

	void WaitForFence(ID3D12Fence* fence, std::uint64_t value) {
		if (fence->GetCompletedValue() >= value) {
			return;
		}
		auto event = CreateManagedEvent();
		ThrowIfFailed(fence->SetEventOnCompletion(value, event.impl.get()));
		event.impl.wait();
	}

}
#endif // defined(_WIN32)
