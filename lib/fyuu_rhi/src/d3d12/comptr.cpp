// Force emission of the Microsoft::WRL::ComPtr destructor symbols that FyuuRHI
// module partitions reference across the module boundary.
//
// The destructors are inline in wrl/client.h; when used inside a C++ module
// partition that includes the header only in its global module fragment, clang
// never emits a standalone symbol for them. Cross-backend variants (for example
// std::variant<..., d3d12::Pipeline, ...>) destroyed in other partitions
// reference the destructors without seeing the definition, which links as
// undefined symbols. Explicitly instantiating each destructor here, in a plain
// translation unit where the header definitions are visible, emits a weak
// symbol that the linker resolves. MSVC emits these inline members natively and
// does not accept destructor explicit-instantiation syntax, so the workaround
// (and its clang-only pragma) is guarded to clang.
#if defined(_WIN32) && defined(__clang__)
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

// The list below is derived from the d3d12 types that appear as alternatives in
// the front-end variants (Resource, View, Sampler, Pipeline, PipelineResourceGroup,
// PhysicalDevice, LogicalDevice, CommandSchedulerContext, CompletionToken) plus
// the ComPtr members of the helpers those types embed by value
// (DescriptorAllocator, DeviceRemovalTracker). Destroying one of those variants
// from any backend partition instantiates every alternative's destructor, so
// adding a ComPtr member to any of those types means adding it here too.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdtor-name"
template void Microsoft::WRL::ComPtr<D3D12MA::Allocation>::~ComPtr();
template void Microsoft::WRL::ComPtr<D3D12MA::Allocator>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12CommandAllocator>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12CommandQueue>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12CommandSignature>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12Device>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12Fence>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12InfoQueue1>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12PipelineState>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12Resource>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12RootSignature>::~ComPtr();
template void Microsoft::WRL::ComPtr<IDXGIAdapter1>::~ComPtr();
template void Microsoft::WRL::ComPtr<IDXGIFactory2>::~ComPtr();
template void Microsoft::WRL::ComPtr<IDXGIFactory5>::~ComPtr();
#pragma clang diagnostic pop
#endif // defined(_WIN32) && defined(__clang__)
