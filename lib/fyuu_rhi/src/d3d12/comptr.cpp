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
// symbol that the linker resolves.
#if defined(_WIN32)
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdtor-name"
template void Microsoft::WRL::ComPtr<D3D12MA::Allocation>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12PipelineState>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12RootSignature>::~ComPtr();
template void Microsoft::WRL::ComPtr<ID3D12CommandSignature>::~ComPtr();
template void Microsoft::WRL::ComPtr<D3D12MA::Allocator>::~ComPtr();
#pragma clang diagnostic pop
#endif // defined(_WIN32)
