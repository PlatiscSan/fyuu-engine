module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>
#include <algorithm>
#include <string>
#include <limits>

#include <cstdint>
#include <array>

#include <string_view>
#include <filesystem>

#include <ranges>
#include <span>
#include <format>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <directx/d3dx12.h>
#include <dxgiformat.h>
#include <wrl.h>
#include <wil/resource.h>
#endif // defined(_WIN32)
#include <slang.h>
#include <boost/hash2/xxhash.hpp>

module fyuu_rhi:d3d12_logical_device;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :cache;
import :command_scheduler_factory;
import :d3d12_data;
import :d3d12_utility;
import :logical_device_dispatch;
import :pipeline;
import :pipeline_factory;
import :resource_factory;
import :sampler;
import :sampler_factory;
import :slang;
import plastic.lru;
import plastic.static_hash_table;
import plastic.static_list;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;

	D3D12MA::ALLOCATION_FLAGS AllocationFlags(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		if (flags.TestMultipleInRange(Bits::UndedicatedAllocation, Bits::DedicatedAllocation)) {
			throw std::invalid_argument(
				"A D3D12 resource cannot request both undedicated and dedicated allocation"
			);
		}
		if (flags.TestMultipleInRange(Bits::MinOffsetAllocation, Bits::FirstFitAllocation)) {
			throw std::invalid_argument(
				"A D3D12 resource cannot request multiple allocation strategies"
			);
		}

		auto result = D3D12MA::ALLOCATION_FLAG_NONE;
		if (flags.Test(Bits::UndedicatedAllocation)) {
			result |= D3D12MA::ALLOCATION_FLAG_NEVER_ALLOCATE;
		}
		else if (flags.Test(Bits::DedicatedAllocation)) {
			result |= D3D12MA::ALLOCATION_FLAG_COMMITTED;
		}
		if (flags.Test(Bits::AllocationWithinBudget)) {
			result |= D3D12MA::ALLOCATION_FLAG_WITHIN_BUDGET;
		}
		if (flags.Test(Bits::AllocationAtUpperAddress)) {
			result |= D3D12MA::ALLOCATION_FLAG_UPPER_ADDRESS;
		}
		if (flags.Test(Bits::AllocationAliasAllowed)) {
			result |= D3D12MA::ALLOCATION_FLAG_CAN_ALIAS;
		}
		if (flags.Test(Bits::MinOffsetAllocation)) {
			result |= D3D12MA::ALLOCATION_FLAG_STRATEGY_MIN_OFFSET;
		}
		else if (flags.Test(Bits::BestFitAllocation)) {
			result |= D3D12MA::ALLOCATION_FLAG_STRATEGY_BEST_FIT;
		}
		else if (flags.Test(Bits::FirstFitAllocation)) {
			result |= D3D12MA::ALLOCATION_FLAG_STRATEGY_FIRST_FIT;
		}
		return result;
	}

	D3D12_HEAP_TYPE HeapType(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		if (flags.TestMultipleInRange(Bits::DeviceLocal, Bits::DeviceReadback)) {
			throw std::invalid_argument("A D3D12 resource cannot use multiple heap types");
		}
		if (flags.Test(Bits::HostVisible)) {
			return D3D12_HEAP_TYPE_UPLOAD;
		}
		if (flags.Test(Bits::DeviceReadback)) {
			return D3D12_HEAP_TYPE_READBACK;
		}
		return D3D12_HEAP_TYPE_DEFAULT;
	}

	D3D12_RESOURCE_STATES InitialState(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		if (flags.Test(Bits::HostVisible)) {
			return D3D12_RESOURCE_STATE_GENERIC_READ;
		}
		if (flags.Test(Bits::DeviceReadback)) {
			return D3D12_RESOURCE_STATE_COPY_DEST;
		}
		return D3D12_RESOURCE_STATE_COMMON;
	}

	D3D12_RESOURCE_FLAGS NativeResourceFlags(ResourceFlags const& flags) noexcept {
		using Bits = ResourceFlagBits;
		auto result = D3D12_RESOURCE_FLAG_NONE;
		bool is_unordered_access = 	
			flags.Test(Bits::StorageTexelBuffer) ||
			flags.Test(Bits::StorageBuffer) ||
			flags.Test(Bits::StorageBinding) ||
			flags.Test(Bits::StorageAttachment);
		if (is_unordered_access) {
			result |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		if (flags.Test(Bits::RenderAttachment)) {
			bool is_depth_stencil = 
				flags.Test(Bits::D16Unorm) ||
				flags.Test(Bits::D24UnormS8Uint) ||
				flags.Test(Bits::D32Float) ||
				flags.Test(Bits::D32FloatS8X24Uint);
			result |= is_depth_stencil ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}
		return result;
	}

	D3D12_TEXTURE_LAYOUT TextureLayout(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		if (flags.Test(Bits::HostVisible) || flags.Test(Bits::DeviceReadback)) {
			return D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		}
		return D3D12_TEXTURE_LAYOUT_UNKNOWN;
	}

	UINT SampleQuality(Microsoft::WRL::ComPtr<ID3D12Device> const& device, DXGI_FORMAT format, UINT sample_count) {
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS query{
			.Format = format,
			.SampleCount = sample_count,
			.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,
			.NumQualityLevels = 0u
		};
		auto result = device->CheckFeatureSupport(
			D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			&query,
			sizeof(query)
		);
		if (FAILED(result) || query.NumQualityLevels == 0u) {
			throw std::invalid_argument(
				std::format(
					"D3D12 sample count {} is unsupported for DXGI format {}",
					sample_count,
					static_cast<UINT>(format)
				)
			);
		}
		return 0u;
	}

	D3D12_CLEAR_VALUE ClearValue(DXGI_FORMAT format) noexcept {
		D3D12_CLEAR_VALUE result{ .Format = format };
		if (fyuu_rhi::d3d12::IsDepthStencil(format)) {
			result.DepthStencil = {
				.Depth = 1.0f,
				.Stencil = 0u
			};
		}
		else {
			result.Color[3u] = 1.0f;
		}
		return result;
	}

	// ---------------------------------------------------------------------------
	// Pipeline helpers
	// ---------------------------------------------------------------------------

	DXGI_FORMAT MapFormat(ResourceFlagBits format) {
		return fyuu_rhi::d3d12::ResourceFormat(ResourceFlags{ format }, true);
	}

	UINT MapSampleCount(ResourceFlagBits sample_count) {
		return fyuu_rhi::d3d12::SampleCount(ResourceFlags{ sample_count });
	}

	D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeType(SlangPipelineBinding const& binding) {
		if (binding.flags.Test(ResourceFlagBits::SamplerBinding)) {
			if (binding.flags.Count() != 1) {
				throw std::invalid_argument(
					"D3D12 does not support a combined texture/sampler pipeline binding"
				);
			}
			return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		}
		if (binding.flags.Test(ResourceFlagBits::UniformBuffer)) {
			return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		}
		if (
			binding.flags.Test(ResourceFlagBits::StorageBuffer) ||
			binding.flags.Test(ResourceFlagBits::StorageBinding)
		) {
			return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		}
		if (binding.flags.Test(ResourceFlagBits::TextureBinding)) {
			return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		}
		throw std::invalid_argument(
			std::format("Unsupported D3D12 pipeline binding '{}'", binding.name)
		);
	}

	std::string RootSignatureCacheKey(SlangPipelineInterface const& pipeline_interface) {
		boost::hash2::xxhash_64 hash;
		constexpr std::uint32_t schema = 1;
		hash.update(&schema, sizeof(schema));
		for (auto const& entry : pipeline_interface.bindings) {
			auto name_size = entry.name.size();
			hash.update(&name_size, sizeof(name_size));
			hash.update(entry.name.data(), entry.name.size());
			auto flags = entry.flags.Snapshot();
			hash.update(flags.data(), sizeof(flags));
			hash.update(&entry.slot, sizeof(entry.slot));
			hash.update(&entry.space, sizeof(entry.space));
			hash.update(&entry.count, sizeof(entry.count));
			hash.update(&entry.visibility, sizeof(entry.visibility));
		}
		for (auto const& range : pipeline_interface.push_constants) {
			hash.update(&range.offset, sizeof(range.offset));
			hash.update(&range.size, sizeof(range.size));
			hash.update(&range.slot, sizeof(range.slot));
			hash.update(&range.space, sizeof(range.space));
			hash.update(&range.visibility, sizeof(range.visibility));
		}
		return std::format("d3d12-root-signature-{:016x}.bin", hash.result());
	}

	Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(d3d12::LogicalDevice const* logical_device, SlangPipelineInterface const& pipeline_interface) {
		auto cache_path = cache::GetCacheFilePath(RootSignatureCacheKey(pipeline_interface));
		auto serialized = cache::ReadFile(cache_path);

		if (serialized.empty()) {
			std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
			std::vector<D3D12_ROOT_PARAMETER1> parameters;
			ranges.reserve(pipeline_interface.bindings.size() * 2u);
			parameters.reserve(
				pipeline_interface.bindings.size() * 2u + pipeline_interface.push_constants.size()
			);

			for (auto const& entry : pipeline_interface.bindings) {
				// A combined texture+sampler binding (Sampler2D) decomposes into a
				// Texture2D at t<slot> and a SamplerState at s<slot>. They live in
				// different descriptor-heap types, so it contributes two root
				// parameters: the SRV table followed by the sampler table.
				bool combined =
					entry.flags.Test(ResourceFlagBits::TextureBinding) &&
					entry.flags.Test(ResourceFlagBits::SamplerBinding);
				auto AddRangeAndParameter = [&](D3D12_DESCRIPTOR_RANGE_TYPE type) {
					ranges.push_back(
						{
							.RangeType = type,
							.NumDescriptors = entry.count,
							.BaseShaderRegister = entry.slot,
							.RegisterSpace = entry.space,
							.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
							.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
						}
					);
					parameters.push_back(
						{
							.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
							.DescriptorTable = {
								.NumDescriptorRanges = 1,
								.pDescriptorRanges = &ranges.back()
							},
							.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
						}
					);
					};
				if (combined) {
					AddRangeAndParameter(D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
					AddRangeAndParameter(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
				}
				else {
					AddRangeAndParameter(DescriptorRangeType(entry));
				}
			}
			for (auto const& range : pipeline_interface.push_constants) {
				if (
					range.offset != 0 ||
					range.size == 0 ||
					range.size % sizeof(std::uint32_t) != 0
				) {
					throw std::invalid_argument(
						"D3D12 push constants must be a non-empty, four-byte-aligned range starting at offset zero"
					);
				}
				parameters.push_back(
					{
						.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
						.Constants = {
							.ShaderRegister = range.slot,
							.RegisterSpace = range.space,
							.Num32BitValues = static_cast<UINT>(range.size / sizeof(std::uint32_t))
						},
						.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
					}
				);
			}

			D3D12_VERSIONED_ROOT_SIGNATURE_DESC descriptor{
				.Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
				.Desc_1_1 = {
					.NumParameters = static_cast<UINT>(parameters.size()),
					.pParameters = parameters.data(),
					.NumStaticSamplers = 0,
					.pStaticSamplers = nullptr,
					.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
				}
			};
			Microsoft::WRL::ComPtr<ID3DBlob> blob;
			Microsoft::WRL::ComPtr<ID3DBlob> errors;
			auto result = D3D12SerializeVersionedRootSignature(&descriptor, &blob, &errors);
			if (FAILED(result)) {
				auto message = errors ? 
					std::string_view(
						static_cast<char const*>(errors->GetBufferPointer()),
						errors->GetBufferSize()
					) : 
					std::string_view{};
				throw std::runtime_error(std::format("D3D12 root signature serialization failed: {}", message));
			}
			auto begin = static_cast<std::byte const*>(blob->GetBufferPointer());
			serialized.assign(begin, begin + blob->GetBufferSize());
			if (!cache::WriteFileAtomically(cache_path, serialized)) {
				throw std::runtime_error("Failed to write D3D12 root-signature cache");
			}
		}

		Microsoft::WRL::ComPtr<ID3D12RootSignature> result;
		d3d12::ThrowIfFailed(
			logical_device->impl->CreateRootSignature(
				0,
				serialized.data(),
				serialized.size(),
				IID_PPV_ARGS(&result)
			)
		);
		return result;
	}

	D3D12_PRIMITIVE_TOPOLOGY_TYPE MapTopologyType(PrimitiveTopology topology) {
		switch (topology) {
		case PrimitiveTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case PrimitiveTopology::LineList:
		case PrimitiveTopology::LineStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case PrimitiveTopology::TriangleList:
		case PrimitiveTopology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		default: throw std::invalid_argument("Unsupported D3D12 primitive topology");
		}
	}

	D3D_PRIMITIVE_TOPOLOGY MapTopology(PrimitiveTopology topology) {
		switch (topology) {
		case PrimitiveTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		case PrimitiveTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case PrimitiveTopology::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case PrimitiveTopology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		default: return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		}
	}

	D3D12_COMPARISON_FUNC MapPipelineCompare(CompareOperation operation) {
		switch (operation) {
		case CompareOperation::Never: return D3D12_COMPARISON_FUNC_NEVER;
		case CompareOperation::Less: return D3D12_COMPARISON_FUNC_LESS;
		case CompareOperation::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
		case CompareOperation::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case CompareOperation::Greater: return D3D12_COMPARISON_FUNC_GREATER;
		case CompareOperation::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case CompareOperation::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case CompareOperation::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
		default: return D3D12_COMPARISON_FUNC_ALWAYS;
		}
	}

	D3D12_STENCIL_OP MapStencilOperation(StencilOperation operation) {
		switch (operation) {
		case StencilOperation::Keep: return D3D12_STENCIL_OP_KEEP;
		case StencilOperation::Zero: return D3D12_STENCIL_OP_ZERO;
		case StencilOperation::Replace: return D3D12_STENCIL_OP_REPLACE;
		case StencilOperation::Invert: return D3D12_STENCIL_OP_INVERT;
		case StencilOperation::IncrementClamp: return D3D12_STENCIL_OP_INCR_SAT;
		case StencilOperation::DecrementClamp: return D3D12_STENCIL_OP_DECR_SAT;
		case StencilOperation::IncrementWrap: return D3D12_STENCIL_OP_INCR;
		case StencilOperation::DecrementWrap: return D3D12_STENCIL_OP_DECR;
		default: return D3D12_STENCIL_OP_KEEP;
		}
	}

	D3D12_DEPTH_STENCILOP_DESC MapStencilFace(StencilFaceState const& source) {
		return {
			.StencilFailOp = MapStencilOperation(source.fail_operation),
			.StencilDepthFailOp = MapStencilOperation(source.depth_fail_operation),
			.StencilPassOp = MapStencilOperation(source.pass_operation),
			.StencilFunc = MapPipelineCompare(source.compare)
		};
	}

	D3D12_BLEND MapBlendFactor(BlendFactor factor) {
		switch (factor) {
		case BlendFactor::Zero: return D3D12_BLEND_ZERO;
		case BlendFactor::One: return D3D12_BLEND_ONE;
		case BlendFactor::SourceColor: return D3D12_BLEND_SRC_COLOR;
		case BlendFactor::OneMinusSourceColor: return D3D12_BLEND_INV_SRC_COLOR;
		case BlendFactor::SourceAlpha: return D3D12_BLEND_SRC_ALPHA;
		case BlendFactor::OneMinusSourceAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
		case BlendFactor::DestinationColor: return D3D12_BLEND_DEST_COLOR;
		case BlendFactor::OneMinusDestinationColor: return D3D12_BLEND_INV_DEST_COLOR;
		case BlendFactor::DestinationAlpha: return D3D12_BLEND_DEST_ALPHA;
		case BlendFactor::OneMinusDestinationAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
		case BlendFactor::SourceAlphaSaturated: return D3D12_BLEND_SRC_ALPHA_SAT;
		case BlendFactor::Constant: return D3D12_BLEND_BLEND_FACTOR;
		case BlendFactor::OneMinusConstant: return D3D12_BLEND_INV_BLEND_FACTOR;
		default: return D3D12_BLEND_ONE;
		}
	}

	D3D12_BLEND_OP MapBlendOperation(BlendOperation operation) {
		switch (operation) {
		case BlendOperation::Add: return D3D12_BLEND_OP_ADD;
		case BlendOperation::Subtract: return D3D12_BLEND_OP_SUBTRACT;
		case BlendOperation::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
		case BlendOperation::Min: return D3D12_BLEND_OP_MIN;
		case BlendOperation::Max: return D3D12_BLEND_OP_MAX;
		default: return D3D12_BLEND_OP_ADD;
		}
	}

	slang::TargetDesc MakeDXILTarget(d3d12::LogicalDevice const* logical_device) {
		auto global_session = shader::SlangGlobalSession();
		char const* profile = "sm_5_1";
		D3D12_FEATURE_DATA_SHADER_MODEL sm_feature = {
			.HighestShaderModel = D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_7
		};
		HRESULT hr = logical_device->impl->CheckFeatureSupport(
			D3D12_FEATURE_SHADER_MODEL,
			&sm_feature,
			sizeof(D3D12_FEATURE_DATA_SHADER_MODEL)
		);
		if (SUCCEEDED(hr)) {
			switch (sm_feature.HighestShaderModel) {
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_7: profile = "sm_6_7"; break;
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_6: profile = "sm_6_6"; break;
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_5: profile = "sm_6_5"; break;
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_4: profile = "sm_6_4"; break;
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_3: profile = "sm_6_3"; break;
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_2: profile = "sm_6_2"; break;
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_1: profile = "sm_6_1"; break;
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_0: profile = "sm_6_0"; break;
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_5_1: profile = "sm_5_1"; break;
			default: break;
			}
		}
		return { .format = SLANG_DXIL, .profile = global_session->findProfile(profile) };
	}

	std::array<std::uint16_t, 4u> QueryDXDriverVersion(DXGI_ADAPTER_DESC1 const& desc) {
		using DXDriverVersion = std::array<std::uint16_t, 4u>;

		struct LUIDHasher {
			std::size_t operator()(LUID const& key) const noexcept {
				return static_cast<std::size_t>(key.LowPart) ^
					(static_cast<std::size_t>(static_cast<std::uint32_t>(key.HighPart)) << (sizeof(std::size_t) * 4));
			}
		};
		struct LUIDEqual {
			bool operator()(LUID const& a, LUID const& b) const noexcept {
				return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
			}
		};
		using List = plastic::ds::StaticList<std::pair<LUID const, DXDriverVersion>, 8u>;
		static plastic::ds::LRUCache<
			plastic::ds::StaticHashTable<LUID, typename List::iterator, 8u, LUIDHasher, LUIDEqual>,
			List,
			8u
		> cache;
		static std::mutex mutex;

		if (std::unique_lock<std::mutex> lock(mutex); cache.Contains(desc.AdapterLuid)) {
			return cache.Get(desc.AdapterLuid);
		}

		// Fetch the driver version from the registry, keyed by adapter LUID.
		wil::unique_hkey dx_key_handle;
		DWORD num_of_adapters = 0;
		LSTATUS return_code = ::RegOpenKeyEx(
			HKEY_LOCAL_MACHINE,
			TEXT("SOFTWARE\\Microsoft\\DirectX"),
			0,
			KEY_READ,
			&dx_key_handle
		);
		if (return_code != ERROR_SUCCESS) {
			return {};
		}

		DWORD sub_key_max_length = 0;
		return_code = ::RegQueryInfoKey(
			dx_key_handle.get(),
			nullptr,
			nullptr,
			nullptr,
			&num_of_adapters,
			&sub_key_max_length,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr
		);
		if (return_code != ERROR_SUCCESS) {
			return {};
		}
		sub_key_max_length += 1; // include the null character

		std::uint64_t driver_version_raw = 0;
		bool is_subkey_found = false;
		std::vector<TCHAR> sub_key_name(sub_key_max_length);

		for (DWORD index = 0; index < num_of_adapters; ++index) {
			DWORD sub_key_length = sub_key_max_length;
			return_code = ::RegEnumKeyEx(
				dx_key_handle.get(),
				index,
				sub_key_name.data(),
				&sub_key_length,
				nullptr,
				nullptr,
				nullptr,
				nullptr
			);
			if (return_code != ERROR_SUCCESS) {
				continue;
			}
			LUID adapter_luid = {};
			DWORD qword_size = sizeof(std::uint64_t);
			return_code = ::RegGetValue(
				dx_key_handle.get(),
				sub_key_name.data(),
				TEXT("AdapterLuid"),
				RRF_RT_QWORD,
				nullptr,
				&adapter_luid,
				&qword_size
			);
			if (
				return_code == ERROR_SUCCESS &&
				adapter_luid.HighPart == desc.AdapterLuid.HighPart &&
				adapter_luid.LowPart == desc.AdapterLuid.LowPart
			) {
				return_code = ::RegGetValue(
					dx_key_handle.get(),
					sub_key_name.data(),
					TEXT("DriverVersion"),
					RRF_RT_QWORD,
					nullptr,
					&driver_version_raw,
					&qword_size
				);
				if (return_code == ERROR_SUCCESS) {
					is_subkey_found = true;
					break;
				}
			}
		}
		if (!is_subkey_found) {
			return {};
		}

		DXDriverVersion dx_driver_version{
			static_cast<std::uint16_t>(driver_version_raw >> 48u),
			static_cast<std::uint16_t>(driver_version_raw >> 32u),
			static_cast<std::uint16_t>(driver_version_raw >> 16u),
			static_cast<std::uint16_t>(driver_version_raw)
		};
		{
			std::unique_lock<std::mutex> lock(mutex);
			cache.Put(desc.AdapterLuid, dx_driver_version);
		}
		return dx_driver_version;
	}

} // namespace

namespace fyuu_rhi {

	template <>
	struct CreateBuffer<d3d12::LogicalDevice> {
		d3d12::LogicalDevice* logical_device;

		Resource operator()(std::size_t size_in_bytes, ResourceFlags const& flags) const {
			D3D12MA::ALLOCATION_DESC allocation_descriptor{
				.Flags = AllocationFlags(flags),
				.HeapType = HeapType(flags),
				.ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
			};
			auto resource_descriptor = CD3DX12_RESOURCE_DESC::Buffer(
				size_in_bytes,
				NativeResourceFlags(flags)
			);
			Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
			d3d12::ThrowIfFailed(
				logical_device->memory_allocator->CreateResource(
					&allocation_descriptor,
					&resource_descriptor,
					InitialState(flags),
					nullptr,
					&allocation,
					IID_NULL,
					nullptr
				)
			);
			return MakeResource(
					d3d12::Resource{
						logical_device->resource_descriptors,
						logical_device->render_target_descriptors,
						logical_device->depth_stencil_descriptors,
						std::move(allocation)
				},
				size_in_bytes,
				flags
			);
		}
	};

	template <>
	struct CreateTexture<d3d12::LogicalDevice> {
		d3d12::LogicalDevice* logical_device;

		Resource operator()(
			std::size_t width,
			std::size_t height,
			std::size_t depth_or_array_layers,
			std::size_t mip_levels,
			ResourceFlags const& flags
		) const {
			using Bits = ResourceFlagBits;
			if (flags.TestMultipleInRange(Bits::Texture1D, Bits::Texture3D)) {
				throw std::invalid_argument("A D3D12 texture must have one dimension");
			}

			D3D12MA::ALLOCATION_DESC allocation_descriptor{
				.Flags = AllocationFlags(flags),
				.HeapType = HeapType(flags),
				.ExtraHeapFlags = D3D12_HEAP_FLAG_NONE
			};
			auto format = d3d12::ResourceFormat(flags, true);
			auto sample_count = d3d12::SampleCount(flags);
			auto resource_flags = NativeResourceFlags(flags);
			auto texture_layout = TextureLayout(flags);
			CD3DX12_RESOURCE_DESC resource_descriptor;
			if (flags.Test(Bits::Texture1D)) {
				resource_descriptor = CD3DX12_RESOURCE_DESC::Tex1D(
					format,
					width,
					static_cast<UINT16>(depth_or_array_layers),
					static_cast<UINT16>(mip_levels),
					resource_flags,
					texture_layout
				);
			}
			else if (flags.Test(Bits::Texture3D)) {
				resource_descriptor = CD3DX12_RESOURCE_DESC::Tex3D(
					format,
					width,
					static_cast<UINT>(height),
					static_cast<UINT16>(depth_or_array_layers),
					static_cast<UINT16>(mip_levels),
					resource_flags,
					texture_layout
				);
			}
			else {
				resource_descriptor = CD3DX12_RESOURCE_DESC::Tex2D(
					format,
					width,
					static_cast<UINT>(height),
					static_cast<UINT16>(depth_or_array_layers),
					static_cast<UINT16>(mip_levels),
					sample_count,
					SampleQuality(logical_device->impl, format,	sample_count),
					resource_flags,
					texture_layout
				);
			}

			auto clear_value = ClearValue(format);
			auto optimized_clear_value = flags.Test(Bits::RenderAttachment)	? &clear_value : nullptr;
			Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
			d3d12::ThrowIfFailed(
				logical_device->memory_allocator->CreateResource(
					&allocation_descriptor,
					&resource_descriptor,
					InitialState(flags),
					optimized_clear_value,
					&allocation,
					IID_NULL,
					nullptr
				)
			);
			return MakeResource(
					d3d12::Resource{
						logical_device->resource_descriptors,
						logical_device->render_target_descriptors,
						logical_device->depth_stencil_descriptors,
						std::move(allocation)
				},
				ResourceTextureExtent{
					.width = static_cast<std::uint32_t>(width),
					.height = static_cast<std::uint32_t>(height),
					.depth_or_array_layers = static_cast<std::uint32_t>(depth_or_array_layers),
					.mip_levels = static_cast<std::uint32_t>(mip_levels)
				},
				flags
			);
		}
	};

	template <>
	struct CreateSampler<d3d12::LogicalDevice> {
		d3d12::LogicalDevice* logical_device;

		Sampler operator()(SamplerDescriptor const& descriptor) const {
			D3D12_SAMPLER_DESC sampler_desc = {
				.Filter = d3d12::BuildFilter(
					descriptor.mag_filter,
					descriptor.min_filter,
					descriptor.mipmap_filter,
					descriptor.max_anisotropy,
					descriptor.compare_function
				),
				.AddressU = d3d12::SamplerAddressMode(descriptor.address_mode_u),
				.AddressV = d3d12::SamplerAddressMode(descriptor.address_mode_v),
				.AddressW = d3d12::SamplerAddressMode(descriptor.address_mode_w),
				.MaxAnisotropy = std::clamp(
					descriptor.max_anisotropy,
					static_cast<std::uint8_t>(1u),
					static_cast<std::uint8_t>(16u)
				),
				.ComparisonFunc = d3d12::ComparisonOperation(descriptor.compare_function),
				.MinLOD = descriptor.min_lod,
				.MaxLOD = descriptor.max_lod
			};
			auto handle = logical_device->sampler_descriptors.Allocate();
			logical_device->impl->CreateSampler(&sampler_desc, handle.CPU());
			return MakeSampler(d3d12::Sampler{ std::move(handle) });
		}
	};

	template <>
	struct CreateGraphicsPipeline<d3d12::LogicalDevice> {
		d3d12::LogicalDevice* logical_device;

		Pipeline operator()(pipeline::GraphicsPipelineDescriptor const& descriptor) const {
			auto target = MakeDXILTarget(logical_device);
			auto adapter = d3d12::GetPhysicalDevice(logical_device->impl);
			DXGI_ADAPTER_DESC1 adapter_desc{};
			d3d12::ThrowIfFailed(adapter->GetDesc1(&adapter_desc));
			auto driver = QueryDXDriverVersion(adapter_desc);
			auto cache_tag = std::format(
				"d3d12-{:04x}-{:04x}-{}.{}.{}.{}",
				adapter_desc.VendorId,
				adapter_desc.DeviceId,
				driver[0], driver[1], driver[2], driver[3]
			);
			shader::SlangProgram program(target, descriptor.program, cache_tag);

			auto bindings = pipeline::MakePipelineBindingMetadata(program.GetInterface());
			auto root_signature = CreateRootSignature(logical_device, program.GetInterface());
			auto primitive_topology = MapTopology(descriptor.primitive.topology);

			D3D12_GRAPHICS_PIPELINE_STATE_DESC native{};
			native.pRootSignature = root_signature.Get();
			for (auto const& entry : program.GetEntryPoints()) {
				D3D12_SHADER_BYTECODE bytecode{ entry.code.data(), entry.code.size() };
				switch (entry.stage) {
				case pipeline::Stage::Vertex: native.VS = bytecode; break;
				case pipeline::Stage::Fragment: native.PS = bytecode; break;
				case pipeline::Stage::TessellationControl: native.HS = bytecode; break;
				case pipeline::Stage::TessellationEvaluation: native.DS = bytecode; break;
				case pipeline::Stage::Geometry: native.GS = bytecode; break;
				case pipeline::Stage::Task:
				case pipeline::Stage::Mesh:
					throw std::invalid_argument(
						"Mesh pipelines require the D3D12 pipeline-state stream path"
					);
				default:
					throw std::invalid_argument(
						"A compute entry point cannot be used by a graphics pipeline"
					);
				}
			}
			if (!native.VS.pShaderBytecode) {
				throw std::invalid_argument("D3D12 graphics pipeline has no vertex entry point");
			}

			auto const& reflected_inputs = program.GetInterface().vertex_inputs;
			auto const& vertex_buffers = descriptor.vertex.buffers;
			std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements =
				descriptor.vertex.attributes | 
				std::views::transform(
					[&](auto const& attribute) {
						auto reflected = std::ranges::find(
							reflected_inputs,
							attribute.location,
							&pipeline::SlangPipelineVertexInput::location
						);
						if (reflected == reflected_inputs.end()) {
							throw std::invalid_argument(
								std::format("No reflected vertex input at location {}", attribute.location)
							);
						}
						auto buffer = std::ranges::find(
							vertex_buffers,
							attribute.slot,
							&pipeline::VertexBufferLayout::slot
						);
						if (buffer == vertex_buffers.end()) {
							throw std::invalid_argument(
								std::format("No vertex buffer layout for slot {}", attribute.slot)
							);
						}
						return D3D12_INPUT_ELEMENT_DESC{
							.SemanticName = reflected->semantic_name.c_str(),
							.SemanticIndex = reflected->semantic_index,
							.Format = MapFormat(attribute.format),
							.InputSlot = attribute.slot,
							.AlignedByteOffset = attribute.offset,
							.InputSlotClass = buffer->input_rate == pipeline::VertexInputRate::Vertex
								? D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
								: D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
							.InstanceDataStepRate = buffer->input_rate == pipeline::VertexInputRate::Vertex ? 0u : 1u
						};
					}
				) | 
				std::ranges::to<std::vector>();
			
			native.InputLayout = { input_elements.data(), static_cast<UINT>(input_elements.size()) };
			native.PrimitiveTopologyType = MapTopologyType(descriptor.primitive.topology);
			if (descriptor.primitive.strip_index_format) {
				if (
					descriptor.primitive.topology != pipeline::PrimitiveTopology::LineStrip &&
					descriptor.primitive.topology != pipeline::PrimitiveTopology::TriangleStrip
				) {
					throw std::invalid_argument(
						"D3D12 strip index format requires a strip topology"
					);
				}
				native.IBStripCutValue = *descriptor.primitive.strip_index_format == pipeline::IndexFormat::Uint16 ? 
					D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF : 
					D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
			}

			native.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			native.RasterizerState.FrontCounterClockwise = descriptor.rasterization.front_face == pipeline::FrontFace::CounterClockwise;
			native.RasterizerState.CullMode = 
				descriptor.rasterization.cull_mode == pipeline::CullMode::Front ? 
				D3D12_CULL_MODE_FRONT :
					descriptor.rasterization.cull_mode == pipeline::CullMode::Back ? 
					D3D12_CULL_MODE_BACK :
					D3D12_CULL_MODE_NONE;
			native.RasterizerState.DepthBias = descriptor.rasterization.depth_bias.constant;
			native.RasterizerState.SlopeScaledDepthBias = descriptor.rasterization.depth_bias.slope_scale;
			native.RasterizerState.DepthBiasClamp = descriptor.rasterization.depth_bias.clamp;

			native.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			native.BlendState.AlphaToCoverageEnable = descriptor.multisample.alpha_to_coverage_enabled;
			if (descriptor.color_targets.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) {
				throw std::invalid_argument("D3D12 supports at most eight color targets");
			}
			native.NumRenderTargets = static_cast<UINT>(descriptor.color_targets.size());
			for (std::size_t index = 0; index < descriptor.color_targets.size(); ++index) {
				auto const& target_state = descriptor.color_targets[index];
				native.RTVFormats[index] = MapFormat(target_state.format);
				auto& target_blend = native.BlendState.RenderTarget[index];
				target_blend.RenderTargetWriteMask = static_cast<UINT8>(target_state.write_mask);
				if (target_state.blend) {
					target_blend.BlendEnable = true;
					target_blend.SrcBlend = MapBlendFactor(target_state.blend->color.source_factor);
					target_blend.DestBlend = MapBlendFactor(target_state.blend->color.destination_factor);
					target_blend.BlendOp = MapBlendOperation(target_state.blend->color.operation);
					target_blend.SrcBlendAlpha = MapBlendFactor(target_state.blend->alpha.source_factor);
					target_blend.DestBlendAlpha = MapBlendFactor(target_state.blend->alpha.destination_factor);
					target_blend.BlendOpAlpha = MapBlendOperation(target_state.blend->alpha.operation);
				}
			}

			native.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			native.DepthStencilState.DepthEnable = false;
			native.DepthStencilState.StencilEnable = false;
			if (descriptor.depth_stencil) {
				native.DSVFormat = MapFormat(descriptor.depth_stencil->format);
				native.DepthStencilState.DepthEnable = descriptor.depth_stencil->depth_test_enabled;
				native.DepthStencilState.DepthWriteMask = descriptor.depth_stencil->depth_write_enabled
					? D3D12_DEPTH_WRITE_MASK_ALL
					: D3D12_DEPTH_WRITE_MASK_ZERO;
				native.DepthStencilState.DepthFunc = MapPipelineCompare(descriptor.depth_stencil->depth_compare);
				native.DepthStencilState.StencilEnable = descriptor.depth_stencil->stencil_enabled;
				native.DepthStencilState.StencilReadMask = static_cast<UINT8>(descriptor.depth_stencil->stencil_read_mask);
				native.DepthStencilState.StencilWriteMask = static_cast<UINT8>(descriptor.depth_stencil->stencil_write_mask);
				native.DepthStencilState.FrontFace = MapStencilFace(descriptor.depth_stencil->stencil_front);
				native.DepthStencilState.BackFace = MapStencilFace(descriptor.depth_stencil->stencil_back);
			}
			native.SampleMask = descriptor.multisample.mask;
			native.SampleDesc = { MapSampleCount(descriptor.multisample.sample_count), 0 };

			// The PSO blob is driver-specific, so the cache key mixes the driver
			// identity into the compiled DXIL and the non-shader PSO descriptor.
			boost::hash2::xxhash_64 pso_hash;
			constexpr std::uint32_t pso_schema = 1;
			pso_hash.update(&pso_schema, sizeof(pso_schema));
			auto root_key = RootSignatureCacheKey(program.GetInterface());
			pso_hash.update(root_key.data(), root_key.size());
			for (auto const& entry : program.GetEntryPoints()) {
				pso_hash.update(&entry.stage, sizeof(entry.stage));
				pso_hash.update(entry.code.data(), entry.code.size());
			}
			auto hash_desc = native;
			hash_desc.pRootSignature = nullptr;
			hash_desc.InputLayout.pInputElementDescs = nullptr;
			hash_desc.VS = {};
			hash_desc.PS = {};
			hash_desc.DS = {};
			hash_desc.HS = {};
			hash_desc.GS = {};
			hash_desc.CachedPSO = {};
			pso_hash.update(&hash_desc, sizeof(hash_desc));
			for (auto const& element : input_elements) {
				pso_hash.update(element.SemanticName, std::char_traits<char>::length(element.SemanticName));
				auto copy = element;
				copy.SemanticName = nullptr;
				pso_hash.update(&copy, sizeof(copy));
			}
			auto pso_path = cache::GetCacheFilePath(
				std::format("d3d12-graphics-pso-{:016x}.bin", pso_hash.result())
			);
			auto cached_pso = cache::ReadFile(pso_path);
			native.CachedPSO = { cached_pso.data(), cached_pso.size() };

			Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
			auto creation_result = logical_device->impl->CreateGraphicsPipelineState(
				&native,
				IID_PPV_ARGS(&pso)
			);
			if (FAILED(creation_result) && !cached_pso.empty()) {
				native.CachedPSO = {};
				cached_pso.clear();
				creation_result = logical_device->impl->CreateGraphicsPipelineState(
					&native,
					IID_PPV_ARGS(&pso)
				);
			}
			d3d12::ThrowIfFailed(creation_result);
			if (cached_pso.empty()) {
				Microsoft::WRL::ComPtr<ID3DBlob> blob;
				d3d12::ThrowIfFailed(pso->GetCachedBlob(&blob));
				auto begin = static_cast<std::byte const*>(blob->GetBufferPointer());
				if (!cache::WriteFileAtomically(
					pso_path,
					std::span<std::byte const>(begin, blob->GetBufferSize())
				)) {
					throw std::runtime_error("Failed to write D3D12 graphics-pipeline cache");
				}
			}
			return MakePipeline(
				d3d12::Pipeline{
					logical_device->resource_descriptors,
					logical_device->sampler_descriptors,
					std::move(root_signature),
					std::move(pso),
					std::move(bindings),
					primitive_topology,
					false
				}
			);
		}
	};

	template <>
	struct CreateComputePipeline<d3d12::LogicalDevice> {
		d3d12::LogicalDevice* logical_device;

		Pipeline operator()(pipeline::ComputePipelineDescriptor const& descriptor) const {
			auto target = MakeDXILTarget(logical_device);
			auto adapter = d3d12::GetPhysicalDevice(logical_device->impl);
			DXGI_ADAPTER_DESC1 adapter_desc{};
			d3d12::ThrowIfFailed(adapter->GetDesc1(&adapter_desc));
			auto driver = QueryDXDriverVersion(adapter_desc);
			auto cache_tag = std::format(
				"d3d12-compute-{:04x}-{:04x}-{}.{}.{}.{}",
				adapter_desc.VendorId,
				adapter_desc.DeviceId,
				driver[0], driver[1], driver[2], driver[3]
			);
			shader::SlangProgram program(target, descriptor.program, cache_tag);

			auto bindings = pipeline::MakePipelineBindingMetadata(program.GetInterface());
			auto root_signature = CreateRootSignature(logical_device, program.GetInterface());

			D3D12_COMPUTE_PIPELINE_STATE_DESC native{};
			native.pRootSignature = root_signature.Get();
			for (auto const& entry : program.GetEntryPoints()) {
				if (entry.stage != pipeline::Stage::Compute) {
					throw std::invalid_argument("D3D12 compute pipelines accept only a compute entry point");
				}
				if (native.CS.pShaderBytecode) {
					throw std::invalid_argument("D3D12 compute pipelines require exactly one compute entry point");
				}
				native.CS = { entry.code.data(), entry.code.size() };
			}
			if (!native.CS.pShaderBytecode) {
				throw std::invalid_argument("D3D12 compute pipelines require one compute entry point");
			}

			boost::hash2::xxhash_64 pso_hash;
			constexpr std::uint32_t pso_schema = 1u;
			pso_hash.update(&pso_schema, sizeof(pso_schema));
			auto root_key = RootSignatureCacheKey(program.GetInterface());
			pso_hash.update(root_key.data(), root_key.size());
			for (auto const& entry : program.GetEntryPoints()) {
				pso_hash.update(&entry.stage, sizeof(entry.stage));
				pso_hash.update(entry.code.data(), entry.code.size());
			}
			auto hash_desc = native;
			hash_desc.pRootSignature = nullptr;
			hash_desc.CS = {};
			hash_desc.CachedPSO = {};
			pso_hash.update(&hash_desc, sizeof(hash_desc));
			auto pso_path = cache::GetCacheFilePath(
				std::format("d3d12-compute-pso-{:016x}.bin", pso_hash.result())
			);
			auto cached_pso = cache::ReadFile(pso_path);
			native.CachedPSO = { cached_pso.data(), cached_pso.size() };

			Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
			auto creation_result = logical_device->impl->CreateComputePipelineState(
				&native,
				IID_PPV_ARGS(&pso)
			);
			if (FAILED(creation_result) && !cached_pso.empty()) {
				native.CachedPSO = {};
				cached_pso.clear();
				creation_result = logical_device->impl->CreateComputePipelineState(
					&native,
					IID_PPV_ARGS(&pso)
				);
			}
			d3d12::ThrowIfFailed(creation_result);
			if (cached_pso.empty()) {
				Microsoft::WRL::ComPtr<ID3DBlob> blob;
				d3d12::ThrowIfFailed(pso->GetCachedBlob(&blob));
				auto begin = static_cast<std::byte const*>(blob->GetBufferPointer());
				if (!cache::WriteFileAtomically(
					pso_path,
					std::span<std::byte const>(begin, blob->GetBufferSize())
				)) {
					throw std::runtime_error("Failed to write D3D12 compute-pipeline cache");
				}
			}
			return MakePipeline(
				d3d12::Pipeline{
					logical_device->resource_descriptors,
					logical_device->sampler_descriptors,
					std::move(root_signature),
					std::move(pso),
					std::move(bindings),
					D3D_PRIMITIVE_TOPOLOGY_UNDEFINED,
					true
				}
			);
		}
	};

	template <>
	struct CreateScheduler<d3d12::LogicalDevice> {
		d3d12::LogicalDevice* logical_device;

		execution::CommandScheduler operator()() const {
			d3d12::CommandSchedulerContext::Queues queues;
			auto create_queue = [&](D3D12_COMMAND_LIST_TYPE type) {
				auto context = std::make_shared<d3d12::QueueContext>();
				D3D12_COMMAND_QUEUE_DESC descriptor{
					.Type = type
				};
				d3d12::ThrowIfFailed(
					logical_device->impl->CreateCommandQueue(
						&descriptor,
						IID_PPV_ARGS(&context->impl)
					)
				);
				d3d12::ThrowIfFailed(
					logical_device->impl->CreateFence(
						0u,
						D3D12_FENCE_FLAG_NONE,
						IID_PPV_ARGS(&context->fence)
					)
				);
				if (type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
					context->presentation_context = std::make_unique<
						d3d12::QueueContext::PresentationContext
					>();
				}
				d3d12::ThrowIfFailed(
					context->impl->SetPrivateDataInterface(
						__uuidof(ID3D12Device),
						logical_device->impl.Get()
					)
				);
				return context;
			};

			auto graphics = create_queue(D3D12_COMMAND_LIST_TYPE_DIRECT);
			queues.emplace(execution::QueueType::Graphics, graphics);
			queues.emplace(execution::QueueType::Present, graphics);
			queues.emplace(
				execution::QueueType::Compute,
				create_queue(D3D12_COMMAND_LIST_TYPE_COMPUTE)
			);
			queues.emplace(
				execution::QueueType::Transfer,
				create_queue(D3D12_COMMAND_LIST_TYPE_COPY)
			);
			return execution::MakeCommandScheduler(
				d3d12::CommandSchedulerContext{
					std::move(queues)
				}
			);
		}
	};

} // namespace fyuu_rhi
#endif // defined(_WIN32)
