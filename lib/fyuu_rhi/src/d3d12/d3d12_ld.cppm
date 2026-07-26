module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <stdexcept>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <algorithm>
#include <mutex>
#include <filesystem>
#include <string_view>
#include <memory_resource>
#include <source_location>
#include <span>
#include <format>
#include <latch>
#include <limits>
#include <memory>
#include <compare>
#include <ranges>
#include <variant>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <D3D12MemAlloc.h>
#include <directx/d3dx12.h>
#include <wrl.h>
#include <comdef.h>
#include <winreg.h>
#include <wil/registry.h>
#endif // defined(_WIN32)
#include <slang.h>
#include <slang-com-helper.h>
#include <slang-com-ptr.h>

#define BOOST_DISABLE_ASSERTS
#include <boost/hash2/xxhash.hpp>
#include <boost/locale.hpp>
#include <boost/scope/defer.hpp>
#include "log.hpp"
module fyuu_rhi:d3d12_logical_dev;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :d3d12_traits;
import :d3d12_utility;
import :resource_types;
import :sampler_types;
import :slang;
import :slang_pipeline_interface;
import :cache_system;
import :native_pipeline_binding;
import plastic.static_list;
import plastic.static_hash_table;
import plastic.lru;

namespace fs = std::filesystem;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::d3d12;

	std::pmr::synchronized_pool_resource s_res_pool{};

	DXGI_FORMAT ExtractFormat(ResourceFlags const& flags);
	UINT ExtractSampleCount(ResourceFlags const& flags);

	std::shared_ptr<Backend::Scheduler::QueueState> CreateSchedulerQueue(
		Backend::LogicalDevice const& ld,
		D3D12_COMMAND_LIST_TYPE type,
		float priority
	) {
		D3D12_COMMAND_QUEUE_DESC descriptor = {
			.Type = type,
			.Priority = priority >= 0.75f ?
				D3D12_COMMAND_QUEUE_PRIORITY_HIGH :
				D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
			.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
			.NodeMask = 0u
		};
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
		ThrowIfFailed(ld.impl->CreateCommandQueue(&descriptor, IID_PPV_ARGS(&queue)));
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		ThrowIfFailed(ld.impl->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		return std::make_shared<Backend::Scheduler::QueueState>(
			ld.impl,
			queue,
			fence,
			1u,
			std::make_shared<Backend::Scheduler::CommandPool>(),
			type
		);
	}

	D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeType(SlangPipelineBinding const& binding) {
		if (binding.flags.Test(ResourceFlagBits::SamplerBinding)) {
			if (binding.flags.Count() != 1) {
				throw std::invalid_argument("D3D12 does not support a combined texture/sampler pipeline binding");
			}
			return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		}
		if (binding.flags.Test(ResourceFlagBits::UniformBuffer)) {
			return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		}
		if (binding.flags.Test(ResourceFlagBits::StorageBuffer) ||
			binding.flags.Test(ResourceFlagBits::StorageBinding)) {
			return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		}
		if (binding.flags.Test(ResourceFlagBits::TextureBinding)) {
			return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		}
		throw std::invalid_argument(std::format("Unsupported D3D12 pipeline binding '{}'", binding.name));
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

	Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(
		Backend::LogicalDevice const& ld,
		SlangPipelineInterface const& pipeline_interface
	) {
		auto cache_path = cache::GetCacheFilePath(RootSignatureCacheKey(pipeline_interface));
		auto serialized = cache::ReadFile(cache_path);

		if (serialized.empty()) {
			std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
			std::vector<D3D12_ROOT_PARAMETER1> parameters;
			ranges.reserve(pipeline_interface.bindings.size());
			parameters.reserve(pipeline_interface.bindings.size() + pipeline_interface.push_constants.size());

			for (auto const& entry : pipeline_interface.bindings) {
				ranges.push_back(
					{
						.RangeType = DescriptorRangeType(entry),
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
						.DescriptorTable = { .NumDescriptorRanges = 1, .pDescriptorRanges = &ranges.back() },
						.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
					}
				);
			}
			for (auto const& range : pipeline_interface.push_constants) {
				if (range.offset != 0 || range.size == 0 || range.size % sizeof(std::uint32_t) != 0) {
					throw std::invalid_argument("D3D12 push constants must be a non-empty, four-byte-aligned range starting at offset zero");
				}
				parameters.push_back(
					{
						.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
						.Constants = {
							.ShaderRegister = range.slot,
							.RegisterSpace = range.space,
							.Num32BitValues = range.size / sizeof(std::uint32_t)
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
				auto message = errors
					? std::string_view(static_cast<char const*>(errors->GetBufferPointer()), errors->GetBufferSize())
					: std::string_view{};
				throw std::runtime_error(std::format("D3D12 root signature serialization failed: {}", message));
			}
			auto begin = static_cast<std::byte const*>(blob->GetBufferPointer());
			serialized.assign(begin, begin + blob->GetBufferSize());
			if (!cache::WriteFileAtomically(cache_path, serialized)) {
				throw std::runtime_error("Failed to write D3D12 root-signature cache");
			}
		}

		Microsoft::WRL::ComPtr<ID3D12RootSignature> result;
		ThrowIfFailed(
			ld.impl->CreateRootSignature(
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

	DXGI_FORMAT MapFormat(ResourceFlagBits format) {
		return ExtractFormat(ResourceFlags(format));
	}

	UINT MapSampleCount(ResourceFlagBits sample_count) {
		return ExtractSampleCount(ResourceFlags(sample_count));
	}

	UINT QueryQualityLevels(ID3D12Device* device, DXGI_FORMAT format, UINT sample_cnt, D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE) {

		struct MSAAQualityCacheKey {
			ID3D12Device* device;
			DXGI_FORMAT fmt;
			UINT sample_cnt;
			D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags;
			std::strong_ordering operator<=>(MSAAQualityCacheKey const& other) const noexcept = default;
		};

		struct MSAAQualityCacheKeyHash {
			std::size_t operator()(MSAAQualityCacheKey const& key) const noexcept {
				std::size_t h1 = std::hash<ID3D12Device*>{}(key.device);
				std::size_t h2 = std::hash<DXGI_FORMAT>{}(key.fmt);
				std::size_t h3 = std::hash<UINT>{}(key.sample_cnt);
				std::size_t h4 = std::hash<UINT>{}(static_cast<UINT>(key.flags));
				return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
			}
		};

		using List = plastic::ds::StaticList<std::pair<MSAAQualityCacheKey const, UINT>, 64u>;

		static plastic::ds::LRUCache<
			plastic::ds::StaticHashTable<MSAAQualityCacheKey, typename List::iterator, 64u, MSAAQualityCacheKeyHash>,
			List,
			64u
		> cache;
		static std::mutex mutex;

		MSAAQualityCacheKey key{ device, format, sample_cnt, flags };
		if (std::unique_lock<std::mutex> lock(mutex); cache.Contains(key)) {
			return cache.Get(key);
		}

		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS query = {
			.Format = format,
			.SampleCount = sample_cnt,
			.Flags = flags,
			.NumQualityLevels = 0,
		};

		HRESULT hr = device->CheckFeatureSupport(
			D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			&query,
			sizeof(query)
		);
		UINT quality_lvls = SUCCEEDED(hr) ? query.NumQualityLevels : 0u;

		{
			std::unique_lock<std::mutex> lock(mutex);
			cache.Put(key, quality_lvls);
		}

		return quality_lvls;

	}

	D3D12MA::ALLOCATION_FLAGS ExtractAllocationFlags(ResourceFlags const& flags) {

		D3D12MA::ALLOCATION_FLAGS d3d12ma_flags{};

		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::UndedicatedAllocation, ResourceFlagBits::DedicatedAllocation);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractAllocationFlags(): UndedicatedAllocation and DedicatedAllocation are set simultaneously");
		}
		
		if (flags.Test(ResourceFlagBits::UndedicatedAllocation)) {
			d3d12ma_flags |= D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_NEVER_ALLOCATE;
		}
		else if (flags.Test(ResourceFlagBits::DedicatedAllocation)) {
			d3d12ma_flags |= D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_COMMITTED;
		}
		else {

		}

		if (flags.Test(ResourceFlagBits::AllocationWithinBudget)) {
			d3d12ma_flags |= D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_WITHIN_BUDGET;
		}
		if (flags.Test(ResourceFlagBits::AllocationAtUpperAddress)) {
			d3d12ma_flags |= D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_UPPER_ADDRESS;
		}
		if (flags.Test(ResourceFlagBits::AllocationAliasAllowed)) {
			d3d12ma_flags |= D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_CAN_ALIAS;
		}

		is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::MinOffsetAllocation, ResourceFlagBits::FirstFitAllocation);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractAllocationFlags(): MinOffsetAllocation BestFitAllocation or FirstFitAllocation are set simultaneously");
		}
		else if (flags.Test(ResourceFlagBits::MinOffsetAllocation)) {
			d3d12ma_flags |= D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_MIN_OFFSET;
		}
		else if (flags.Test(ResourceFlagBits::BestFitAllocation)) {
			d3d12ma_flags |= D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_BEST_FIT;
		}
		else if (flags.Test(ResourceFlagBits::FirstFitAllocation)) {
			d3d12ma_flags |= D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_STRATEGY_FIRST_FIT;
		}
		else {

		}

		return d3d12ma_flags;

	}

	D3D12_HEAP_TYPE ExtractHeapType(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::DeviceLocal, ResourceFlagBits::DeviceReadback);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractHeapType(): DeviceLocal HostVisible or DeviceReadback are set simultaneously");
		}

		if (flags.Test(ResourceFlagBits::DeviceLocal)) {
			return D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT;
		}
		else if (flags.Test(ResourceFlagBits::HostVisible)) {
			return D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_UPLOAD;
		}
		else if (flags.Test(ResourceFlagBits::DeviceReadback)) {
			return D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_READBACK;
		} 
		else {
			return D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT;
		}

	}

	D3D12_RESOURCE_FLAGS ExtractResourceFlags(ResourceFlags const& flags) noexcept {
		
		D3D12_RESOURCE_FLAGS res_flags = D3D12_RESOURCE_FLAG_NONE;

		if (flags.Test(ResourceFlagBits::StorageTexelBuffer) ||
			flags.Test(ResourceFlagBits::StorageBuffer) ||
			flags.Test(ResourceFlagBits::StorageBinding) ||
			flags.Test(ResourceFlagBits::StorageAttachment)) {
			res_flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		if (flags.Test(ResourceFlagBits::RenderAttachment)) {
			bool is_ds =
				flags.Test(ResourceFlagBits::D16Unorm) ||
				flags.Test(ResourceFlagBits::D24UnormS8Uint) ||
				flags.Test(ResourceFlagBits::D32Float) ||
				flags.Test(ResourceFlagBits::D32FloatS8X24Uint);

			if (is_ds) {
				res_flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			}
			else {
				res_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			}
		}

		return res_flags;

	}

	D3D12_RESOURCE_STATES DetermineInitialState(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::DeviceLocal, ResourceFlagBits::DeviceReadback);
		if (is_conflicting) {
			throw std::invalid_argument("DetermineInitialState(): DeviceLocal HostVisible or DeviceReadback are set simultaneously");
		}
		if (flags.Test(ResourceFlagBits::DeviceLocal)) {
			return D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON;
		}
		else if (flags.Test(ResourceFlagBits::HostVisible)) {
			return D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_GENERIC_READ;
		}
		else if (flags.Test(ResourceFlagBits::DeviceReadback)) {
			return D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST;
		}
		else {
			return D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON;
		}
	}

	DXGI_FORMAT ExtractFormat(ResourceFlags const& flags) {

		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::R8Unorm, ResourceFlagBits::Bc7UnormSrgb);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractFormat(): Only one format can be set");
		}

		// 8‑bit per component (1‑channel)
		if (flags.Test(ResourceFlagBits::R8Unorm)) return DXGI_FORMAT_R8_UNORM;
		else if (flags.Test(ResourceFlagBits::R8Snorm)) return DXGI_FORMAT_R8_SNORM;
		else if (flags.Test(ResourceFlagBits::R8Uint)) return DXGI_FORMAT_R8_UINT;
		else if (flags.Test(ResourceFlagBits::R8Sint)) return DXGI_FORMAT_R8_SINT;
	
		// 8‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R8G8Unorm)) return DXGI_FORMAT_R8G8_UNORM;
		else if (flags.Test(ResourceFlagBits::R8G8Snorm)) return DXGI_FORMAT_R8G8_SNORM;
		else if (flags.Test(ResourceFlagBits::R8G8Uint)) return DXGI_FORMAT_R8G8_UINT;
		else if (flags.Test(ResourceFlagBits::R8G8Sint)) return DXGI_FORMAT_R8G8_SINT;
	
		// 8‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Unorm)) return DXGI_FORMAT_R8G8B8A8_UNORM;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Snorm)) return DXGI_FORMAT_R8G8B8A8_SNORM;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Uint)) return DXGI_FORMAT_R8G8B8A8_UINT;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Sint)) return DXGI_FORMAT_R8G8B8A8_SINT;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Srgb)) return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		else if (flags.Test(ResourceFlagBits::B8G8R8A8Srgb)) return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	
		// 16‑bit per component (1‑channel)
		else if (flags.Test(ResourceFlagBits::R16Unorm)) return DXGI_FORMAT_R16_UNORM;
		else if (flags.Test(ResourceFlagBits::R16Snorm)) return DXGI_FORMAT_R16_SNORM;
		else if (flags.Test(ResourceFlagBits::R16Uint)) return DXGI_FORMAT_R16_UINT;
		else if (flags.Test(ResourceFlagBits::R16Sint)) return DXGI_FORMAT_R16_SINT;
		else if (flags.Test(ResourceFlagBits::R16Float)) return DXGI_FORMAT_R16_FLOAT;
	
		// 16‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R16G16Unorm)) return DXGI_FORMAT_R16G16_UNORM;
		else if (flags.Test(ResourceFlagBits::R16G16Snorm)) return DXGI_FORMAT_R16G16_SNORM;
		else if (flags.Test(ResourceFlagBits::R16G16Uint)) return DXGI_FORMAT_R16G16_UINT;
		else if (flags.Test(ResourceFlagBits::R16G16Sint)) return DXGI_FORMAT_R16G16_SINT;
		else if (flags.Test(ResourceFlagBits::R16G16Float)) return DXGI_FORMAT_R16G16_FLOAT;
	
		// 16‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Unorm)) return DXGI_FORMAT_R16G16B16A16_UNORM;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Snorm)) return DXGI_FORMAT_R16G16B16A16_SNORM;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Uint)) return DXGI_FORMAT_R16G16B16A16_UINT;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Sint)) return DXGI_FORMAT_R16G16B16A16_SINT;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Float)) return DXGI_FORMAT_R16G16B16A16_FLOAT;
	
		// 32‑bit per component (1‑channel)
		else if (flags.Test(ResourceFlagBits::R32Uint)) return DXGI_FORMAT_R32_UINT;
		else if (flags.Test(ResourceFlagBits::R32Sint)) return DXGI_FORMAT_R32_SINT;
		else if (flags.Test(ResourceFlagBits::R32Float)) return DXGI_FORMAT_R32_FLOAT;
	
		// 32‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R32G32Uint)) return DXGI_FORMAT_R32G32_UINT;
		else if (flags.Test(ResourceFlagBits::R32G32Sint)) return DXGI_FORMAT_R32G32_SINT;
		else if (flags.Test(ResourceFlagBits::R32G32Float)) return DXGI_FORMAT_R32G32_FLOAT;
	
		// 32‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Uint)) return DXGI_FORMAT_R32G32B32A32_UINT;
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Sint)) return DXGI_FORMAT_R32G32B32A32_SINT;
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Float)) return DXGI_FORMAT_R32G32B32A32_FLOAT;
	
		// Packed formats (note: DXGI uses BGR ordering for 5/6/5 and 5/5/5/1)
		else if (flags.Test(ResourceFlagBits::R10G10B10A2Unorm)) return DXGI_FORMAT_R10G10B10A2_UNORM;
		else if (flags.Test(ResourceFlagBits::R10G10B10A2Uint)) return DXGI_FORMAT_R10G10B10A2_UINT;
		else if (flags.Test(ResourceFlagBits::R11G11B10Float)) return DXGI_FORMAT_R11G11B10_FLOAT;
		else if (flags.Test(ResourceFlagBits::R9G9B9E5SharedExp)) return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
	
		// Depth/stencil
		else if (flags.Test(ResourceFlagBits::D16Unorm)) return DXGI_FORMAT_D16_UNORM;
		else if (flags.Test(ResourceFlagBits::D24UnormS8Uint)) return DXGI_FORMAT_D24_UNORM_S8_UINT;
		else if (flags.Test(ResourceFlagBits::D32Float)) return DXGI_FORMAT_D32_FLOAT;
		else if (flags.Test(ResourceFlagBits::D32FloatS8X24Uint)) return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	
		// BC compressed formats
		else if (flags.Test(ResourceFlagBits::Bc1Unorm)) return DXGI_FORMAT_BC1_UNORM;
		else if (flags.Test(ResourceFlagBits::Bc1UnormSrgb)) return DXGI_FORMAT_BC1_UNORM_SRGB;
		else if (flags.Test(ResourceFlagBits::Bc2Unorm)) return DXGI_FORMAT_BC2_UNORM;
		else if (flags.Test(ResourceFlagBits::Bc2UnormSrgb)) return DXGI_FORMAT_BC2_UNORM_SRGB;
		else if (flags.Test(ResourceFlagBits::Bc3Unorm)) return DXGI_FORMAT_BC3_UNORM;
		else if (flags.Test(ResourceFlagBits::Bc3UnormSrgb)) return DXGI_FORMAT_BC3_UNORM_SRGB;
		else if (flags.Test(ResourceFlagBits::Bc4Unorm)) return DXGI_FORMAT_BC4_UNORM;
		else if (flags.Test(ResourceFlagBits::Bc4Snorm)) return DXGI_FORMAT_BC4_SNORM;
		else if (flags.Test(ResourceFlagBits::Bc5Unorm)) return DXGI_FORMAT_BC5_UNORM;
		else if (flags.Test(ResourceFlagBits::Bc5Snorm)) return DXGI_FORMAT_BC5_SNORM;
		else if (flags.Test(ResourceFlagBits::Bc6HUfloat)) return DXGI_FORMAT_BC6H_UF16;
		else if (flags.Test(ResourceFlagBits::Bc6HSfloat)) return DXGI_FORMAT_BC6H_SF16;
		else if (flags.Test(ResourceFlagBits::Bc7Unorm)) return DXGI_FORMAT_BC7_UNORM;
		else if (flags.Test(ResourceFlagBits::Bc7UnormSrgb)) return DXGI_FORMAT_BC7_UNORM_SRGB;

		else return DXGI_FORMAT_UNKNOWN;

	}

	UINT ExtractSampleCount(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::Sample1, ResourceFlagBits::Sample64);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractSampleCount(): Only sample count can be set");
		}
		else if (flags.Test(ResourceFlagBits::Sample1)) {
			return 1u;
		}
		else if (flags.Test(ResourceFlagBits::Sample2)) {
			return 2u;
		}
		else if (flags.Test(ResourceFlagBits::Sample4)) {
			return 4u;
		}
		else if (flags.Test(ResourceFlagBits::Sample8)) {
			return 8u;
		}
		else if (flags.Test(ResourceFlagBits::Sample16)) {
			return 16u;
		}
		else if (flags.Test(ResourceFlagBits::Sample32)) {
			return 32u;
		}
		else if (flags.Test(ResourceFlagBits::Sample64)) {
			return 64u;
		}
		else {
			return 1u;
		}

	}

	D3D12_TEXTURE_LAYOUT ExtractTextureLayout(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::DeviceLocal, ResourceFlagBits::DeviceReadback);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractTextureLayout(): DeviceLocal HostVisible or DeviceReadback are set simultaneously");
		}
		else if (flags.Test(ResourceFlagBits::DeviceLocal)) {
			return D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_UNKNOWN;
		}
		else if (flags.Test(ResourceFlagBits::HostVisible)) {
			return D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		}
		else if (flags.Test(ResourceFlagBits::DeviceReadback)) {
			return D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		}
		else {
			return D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_UNKNOWN;			
		}
	}

	D3D12_SRV_DIMENSION ExtractSRVDimension(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::TextureView1D, ResourceFlagBits::TextureView3D);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractSRVDimension(): Only one texture view type can be set");
		}
		if (flags.Test(ResourceFlagBits::TextureView1D))
			return D3D12_SRV_DIMENSION_TEXTURE1D;
		else if (flags.Test(ResourceFlagBits::TextureView2D))
			return D3D12_SRV_DIMENSION_TEXTURE2D;
		else if (flags.Test(ResourceFlagBits::TextureView2DArray))
			return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		else if (flags.Test(ResourceFlagBits::TextureView3D))
			return D3D12_SRV_DIMENSION_TEXTURE3D;
		else if (flags.Test(ResourceFlagBits::TextureViewCube))
			return D3D12_SRV_DIMENSION_TEXTURECUBE;
		else if (flags.Test(ResourceFlagBits::TextureViewCubeArray))
			return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
		else 
			return D3D12_SRV_DIMENSION_TEXTURE2D;
	}

	D3D12_UAV_DIMENSION ExtractUAVDimension(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::TextureView1D, ResourceFlagBits::TextureView3D);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractUAVDimension(): Only one texture view type can be set");
		}
		if (flags.Test(ResourceFlagBits::TextureView1D))
			return D3D12_UAV_DIMENSION_TEXTURE1D;
		else if (flags.Test(ResourceFlagBits::TextureView2D))
			return D3D12_UAV_DIMENSION_TEXTURE2D;
		else if (flags.Test(ResourceFlagBits::TextureView2DArray))
			return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		else if (flags.Test(ResourceFlagBits::TextureView3D))
			return D3D12_UAV_DIMENSION_TEXTURE3D;
		else
			return D3D12_UAV_DIMENSION_TEXTURE2D;
	}

	D3D12_RTV_DIMENSION ExtractRTVDimension(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::TextureView1D, ResourceFlagBits::TextureView3D);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractRTVDimension(): Only one texture view type can be set");
		}
		if (flags.Test(ResourceFlagBits::TextureView1D))
			return D3D12_RTV_DIMENSION_TEXTURE1D;
		else if (flags.Test(ResourceFlagBits::TextureView2D))
			return D3D12_RTV_DIMENSION_TEXTURE2D;
		else if (flags.Test(ResourceFlagBits::TextureView2DArray))
			return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		else if (flags.Test(ResourceFlagBits::TextureView3D))
			return D3D12_RTV_DIMENSION_TEXTURE3D;
		else
			return D3D12_RTV_DIMENSION_TEXTURE2D;
	}

	D3D12_DSV_DIMENSION ExtractDSVDimension(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::TextureView1D, ResourceFlagBits::TextureView3D);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractDSVDimension(): Only one texture view type can be set");
		}
		if (flags.Test(ResourceFlagBits::TextureView1D))
			return D3D12_DSV_DIMENSION_TEXTURE1D;
		else if (flags.Test(ResourceFlagBits::TextureView2D))
			return D3D12_DSV_DIMENSION_TEXTURE2D;
		else if (flags.Test(ResourceFlagBits::TextureView2DArray))
			return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		else
			return D3D12_DSV_DIMENSION_TEXTURE2D;
	}

	bool IsDepthStencilFormat(DXGI_FORMAT format) noexcept {
		switch (format) {
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			return true;
		default:
			return false;
		}
	}

	D3D12_CLEAR_VALUE OptimizedClearValue(DXGI_FORMAT format) noexcept {
		D3D12_CLEAR_VALUE result{ .Format = format };
		if (IsDepthStencilFormat(format)) {
			result.DepthStencil = {
				.Depth = 1.0f,
				.Stencil = 0u
			};
		}
		else {
			result.Color[0] = 0.0f;
			result.Color[1] = 0.0f;
			result.Color[2] = 0.0f;
			result.Color[3] = 1.0f;
		}
		return result;
	}

	D3D12_DSV_FLAGS ExtractDSVFlags(ResourceFlags const& flags) noexcept {
		D3D12_DSV_FLAGS dsv_flags = D3D12_DSV_FLAG_NONE;
		if (flags.Test(ResourceFlagBits::TextureViewAspectDepthOnly) &&
			!flags.Test(ResourceFlagBits::TextureViewAspectStencilOnly))
			dsv_flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
		if (flags.Test(ResourceFlagBits::TextureViewAspectStencilOnly) &&
			!flags.Test(ResourceFlagBits::TextureViewAspectDepthOnly))
			dsv_flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		return dsv_flags;
	}

	D3D12_TEXTURE_ADDRESS_MODE MapAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::ClampToEdge:		return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case AddressMode::Repeat:			return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case AddressMode::MirroredRepeat:	return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		default:							return D3D12_TEXTURE_ADDRESS_MODE_CLAMP; // Unknown fallback
		}
	}

	D3D12_COMPARISON_FUNC MapCompareFunction(CompareFunction func) noexcept {
		switch (func) {
		case CompareFunction::Never:		return D3D12_COMPARISON_FUNC_NEVER;
		case CompareFunction::Less:			return D3D12_COMPARISON_FUNC_LESS;
		case CompareFunction::Equal:		return D3D12_COMPARISON_FUNC_EQUAL;
		case CompareFunction::LessEqual:	return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case CompareFunction::Greater:		return D3D12_COMPARISON_FUNC_GREATER;
		case CompareFunction::NotEqual:		return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case CompareFunction::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case CompareFunction::Always:		return D3D12_COMPARISON_FUNC_ALWAYS;
		default:							return D3D12_COMPARISON_FUNC_NONE; // Unknown = no comparison
		}
	}

	D3D12_FILTER_TYPE MapFilterType(FilterMode mode) noexcept {
		return mode == FilterMode::Nearest ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
	}

	D3D12_FILTER_TYPE MapMipmapFilterType(MipmapFilterMode mode) noexcept {
		return mode == MipmapFilterMode::Nearest ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
	}

	D3D12_FILTER BuildFilter(FilterMode mag, FilterMode min, MipmapFilterMode mip, UINT max_anisotropy, CompareFunction compare) noexcept {

		bool anisotropic = max_anisotropy > 1;
		bool comparison = compare != CompareFunction::Unknown;

		if (anisotropic) {
			return comparison ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
		}

		D3D12_FILTER_TYPE mag_type = MapFilterType(mag);
		D3D12_FILTER_TYPE min_type = MapFilterType(min);
		D3D12_FILTER_TYPE mip_type = MapMipmapFilterType(mip);

		UINT filter = D3D12_ENCODE_BASIC_FILTER(min_type, mag_type, mip_type, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
		if (comparison) {
			filter = D3D12_ENCODE_BASIC_FILTER(min_type, mag_type, mip_type, D3D12_FILTER_REDUCTION_TYPE_COMPARISON);
		}

		return static_cast<D3D12_FILTER>(filter);

	}

	/// @brief buffer view only
	/// @param fmt 
	/// @return 
	UINT BitsPerPixel(DXGI_FORMAT fmt) noexcept {
		switch (fmt) {
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
			return 32u;
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
			return 64u;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 96u;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128u;
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_SNORM:
			return 16u;
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_SNORM:
			return 32u;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
			return 64u;
		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_SNORM:
			return 8u;
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_SNORM:
			return 16u;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return 32u;
		default:
			return 0u; // Unknown or unsupported.
		}
	}

	SlangProfileID QueryHighestSMLevel(Backend::LogicalDevice const& ld) {

		D3D12_FEATURE_DATA_SHADER_MODEL sm_feature = {
			.HighestShaderModel = D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_7
		};

		Slang::ComPtr<slang::IGlobalSession> global_session = shader::SlangGlobalSession();

		if (SUCCEEDED(ld.impl->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm_feature, sizeof(D3D12_FEATURE_DATA_SHADER_MODEL)))) {
			switch (sm_feature.HighestShaderModel) {
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_7:
				return global_session->findProfile("sm_6_7");
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_6:
				return global_session->findProfile("sm_6_6");
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_5:
				return global_session->findProfile("sm_6_5");
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_4:
				return global_session->findProfile("sm_6_4");
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_3:
				return global_session->findProfile("sm_6_3");
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_2:
				return global_session->findProfile("sm_6_2");
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_1:
				return global_session->findProfile("sm_6_1");
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_6_0:
				return global_session->findProfile("sm_6_0");
			case D3D_SHADER_MODEL::D3D_SHADER_MODEL_5_1:
				return global_session->findProfile("sm_5_1");
			default:
				std::unreachable();
			}
		}

		return global_session->findProfile("sm_5_1");

	}

	std::array<std::uint16_t, 4u> QueryDXDriverVersion(DXGI_ADAPTER_DESC1 const& desc) noexcept {

		using DXDriverVersion = std::array<std::uint16_t, 4u>;

		struct LUIDHasher {
			std::size_t operator()(LUID const& key) const noexcept {
				return static_cast<std::size_t>(key.LowPart) ^ (static_cast<std::size_t>(static_cast<std::uint32_t>(key.HighPart)) 
					<< (sizeof(std::size_t) * 4));
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
	
		// Fetch registry data
		wil::unique_hkey dx_key_handle;
		DWORD num_of_adapters = 0;
	
		LSTATUS return_code = ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\Microsoft\\DirectX"), 0, KEY_READ, &dx_key_handle);
	
		if (return_code != ERROR_SUCCESS) {
			return {};
		}
	
		// Find all subkeys
	
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
	
		for (DWORD i = 0; i < num_of_adapters; ++i) {

			DWORD sub_key_length = sub_key_max_length;
	
			return_code = ::RegEnumKeyEx(
				dx_key_handle.get(),
				i,
				sub_key_name.data(),
				&sub_key_length,
				nullptr,
				nullptr,
				nullptr,
				nullptr
			);
	
			if (return_code == ERROR_SUCCESS) {

				LUID adapter_luid = {};
				DWORD qword_size = sizeof( uint64_t );
	
				return_code = ::RegGetValue(
					dx_key_handle.get(),
					sub_key_name.data(),
					TEXT("AdapterLuid"),
					RRF_RT_QWORD,
					nullptr,
					&adapter_luid,
					&qword_size
				);
	
				if (return_code == ERROR_SUCCESS &&  // If we were able to retrieve the registry values
					adapter_luid.HighPart == desc.AdapterLuid.HighPart && adapter_luid.LowPart == desc.AdapterLuid.LowPart // and if the vendor ID and device ID match
					) {
					// We have our registry key! Let's get the driver version num now
	
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
		}

		if (!is_subkey_found) {
			return {};
		}
	
		std::array<std::uint16_t, 4u> dx_driver_version;

		// Now that we have our driver version as a DWORD, let's process that into something readable
		dx_driver_version[0u] = static_cast<std::uint16_t>(driver_version_raw >> 48u);
		dx_driver_version[1u] = static_cast<std::uint16_t>(driver_version_raw >> 32u);
		dx_driver_version[2u] = static_cast<std::uint16_t>(driver_version_raw >> 16u);
		dx_driver_version[3u] = static_cast<std::uint16_t>(driver_version_raw);
	
		{
			std::unique_lock<std::mutex> lock(mutex);
			cache.Put(desc.AdapterLuid, dx_driver_version);
		}

		return dx_driver_version;

	}

}

namespace fyuu_rhi::d3d12 {

	using namespace fyuu_rhi::pipeline;

	Backend::Resource Backend::CreateBuffer(Backend::LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags) {
		D3D12MA::ALLOCATION_DESC alloc_desc{
			ExtractAllocationFlags(flags),
			ExtractHeapType(flags),
			D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
			nullptr,
			nullptr
		};
		auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size_in_bytes, ExtractResourceFlags(flags));
		auto init_state = DetermineInitialState(flags);
		Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		HRESULT result = ld.mem_alloc->CreateResource(
			&alloc_desc,
			&res_desc,
			init_state,
			nullptr,
			&allocation,
			IID_PPV_ARGS(&res)
		);
		ThrowIfFailed(result);
		return { std::move(allocation), init_state };
	}

	Backend::Resource Backend::CreateTexture(Backend::LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags) {
		D3D12MA::ALLOCATION_DESC alloc_desc{
			ExtractAllocationFlags(flags),
			ExtractHeapType(flags),
			D3D12_HEAP_FLAG_NONE,
			nullptr,
			nullptr
		};
		DXGI_FORMAT format = ExtractFormat(flags);
		UINT sample_cnt = ExtractSampleCount(flags);
		D3D12_RESOURCE_FLAGS res_flags = ExtractResourceFlags(flags);
		D3D12_TEXTURE_LAYOUT tex_layout = ExtractTextureLayout(flags);
		auto ResourceDescriptor = [&]() {
			bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::Texture1D, ResourceFlagBits::Texture3D);
			if (is_conflicting) {
				throw std::invalid_argument("CreateTexture(): Texture1D Texture2D or Texture3D are set simultaneously");
			}
			if (flags.Test(ResourceFlagBits::Texture1D)) {
				return CD3DX12_RESOURCE_DESC::Tex1D(
					format, width, static_cast<UINT16>(depth_arr_layers), 
					static_cast<UINT16>(mip_lvl_cnt), res_flags, tex_layout
				);
			}
			if (flags.Test(ResourceFlagBits::Texture2D)) {
				return CD3DX12_RESOURCE_DESC::Tex2D(
					format, width, static_cast<UINT>(height), static_cast<UINT16>(depth_arr_layers), 
					static_cast<UINT16>(mip_lvl_cnt), sample_cnt,
					QueryQualityLevels(ld.impl.Get(), format, sample_cnt), 
					res_flags, tex_layout
				);
			}
			if (flags.Test(ResourceFlagBits::Texture3D)) {
				return CD3DX12_RESOURCE_DESC::Tex3D(
					format, width, static_cast<UINT>(height), static_cast<UINT16>(depth_arr_layers), 
					static_cast<UINT16>(mip_lvl_cnt), res_flags, tex_layout
				);
			}
			return CD3DX12_RESOURCE_DESC::Tex2D(
				format, width, static_cast<UINT>(height), static_cast<UINT16>(depth_arr_layers), 
				static_cast<UINT16>(mip_lvl_cnt), sample_cnt,
				QueryQualityLevels(ld.impl.Get(), format, sample_cnt), 
				res_flags, tex_layout
			);
			}();
		auto init_state = DetermineInitialState(flags);
		auto clear_value = OptimizedClearValue(format);
		auto optimized_clear_value = flags.Test(ResourceFlagBits::RenderAttachment)
			? &clear_value
			: nullptr;
		Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		HRESULT result = ld.mem_alloc->CreateResource(
			&alloc_desc,
			&ResourceDescriptor,
			init_state,
			optimized_clear_value,
			&allocation,
			IID_PPV_ARGS(&res)
		);
		ThrowIfFailed(result);

		return { std::move(allocation), init_state };
	}
	
	Backend::View Backend::CreateTextureView(Backend::LogicalDevice& ld, Backend::Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags) {
		
		ID3D12Resource* tex = res.impl->GetResource();
		D3D12_RESOURCE_DESC desc = tex->GetDesc();
		DXGI_FORMAT format = ExtractFormat(flags);

		bool is_ds = IsDepthStencilFormat(format);
		bool is_srv = flags.Test(ResourceFlagBits::TextureBinding);
		bool is_uav = flags.Test(ResourceFlagBits::StorageBinding);
		bool is_rtv = flags.Test(ResourceFlagBits::RenderAttachment) && !is_ds;
		bool is_dsv = flags.Test(ResourceFlagBits::RenderAttachment) && is_ds;

		if ((is_srv && is_uav) || (is_srv && is_rtv) || (is_srv && is_dsv) ||
			(is_uav && is_rtv) || (is_uav && is_dsv) || (is_rtv && is_dsv)) {
			throw std::invalid_argument("CreateTextureView(): conflicting view types in flags");
		}

		if (is_srv) {
			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
				.Format = format,
				.ViewDimension = ExtractSRVDimension(flags),
				.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
			};

			switch (srv_desc.ViewDimension) {
			case D3D12_SRV_DIMENSION_TEXTURE1D:
				srv_desc.Texture1D.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
				srv_desc.Texture1D.MipLevels = static_cast<UINT>(mip_lvl_cnt);
				srv_desc.Texture1D.ResourceMinLODClamp = 0.0f;
				break;
			case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
				srv_desc.Texture1DArray.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
				srv_desc.Texture1DArray.MipLevels = static_cast<UINT>(mip_lvl_cnt);
				srv_desc.Texture1DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
				srv_desc.Texture1DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
				srv_desc.Texture1DArray.ResourceMinLODClamp = 0.0f;
				break;
			case D3D12_SRV_DIMENSION_TEXTURE2D:
				srv_desc.Texture2D.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
				srv_desc.Texture2D.MipLevels = static_cast<UINT>(mip_lvl_cnt);
				srv_desc.Texture2D.PlaneSlice = 0;
				srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
				break;
			case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
				srv_desc.Texture2DArray.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
				srv_desc.Texture2DArray.MipLevels = static_cast<UINT>(mip_lvl_cnt);
				srv_desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
				srv_desc.Texture2DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
				srv_desc.Texture2DArray.PlaneSlice = 0;
				srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
				break;
			case D3D12_SRV_DIMENSION_TEXTURE3D:
				srv_desc.Texture3D.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
				srv_desc.Texture3D.MipLevels = static_cast<UINT>(mip_lvl_cnt);
				srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
				break;
			case D3D12_SRV_DIMENSION_TEXTURECUBE:
				srv_desc.TextureCube.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
				srv_desc.TextureCube.MipLevels = static_cast<UINT>(mip_lvl_cnt);
				srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
				break;
			case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
				if (arr_layer_cnt == 0 || base_arr_layer % 6 != 0 || arr_layer_cnt % 6 != 0) {
					throw std::invalid_argument("Both base_arr_layer and arr_layer_cnt must be a multiple of 6");
				}
				srv_desc.TextureCubeArray.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
				srv_desc.TextureCubeArray.MipLevels = static_cast<UINT>(mip_lvl_cnt);
				srv_desc.TextureCubeArray.First2DArrayFace = static_cast<UINT>(base_arr_layer);
				srv_desc.TextureCubeArray.NumCubes = static_cast<UINT>(arr_layer_cnt / 6);
				srv_desc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
				break;
			default:
				throw std::invalid_argument("CreateTextureView(): Unsupported SRV dimension");
			}

			auto handle = ld.univ_alloc.Allocate();
			ld.impl->CreateShaderResourceView(tex, &srv_desc, handle.CPU());
			return {
				res.impl,
				std::move(handle),
				View::Type::ShaderResource,
				static_cast<std::uint32_t>(base_mip_lvl),
				static_cast<std::uint32_t>(mip_lvl_cnt),
				static_cast<std::uint32_t>(base_arr_layer),
				static_cast<std::uint32_t>(arr_layer_cnt),
				format
			};
		}

		if (is_uav) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
				.Format = format,
				.ViewDimension = ExtractUAVDimension(flags)
			};

			switch (uav_desc.ViewDimension) {
			case D3D12_UAV_DIMENSION_TEXTURE1D:
				uav_desc.Texture1D.MipSlice = static_cast<UINT>(base_mip_lvl);
				break;
			case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
				uav_desc.Texture1DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
				uav_desc.Texture1DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
				uav_desc.Texture1DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
				break;
			case D3D12_UAV_DIMENSION_TEXTURE2D:
				uav_desc.Texture2D.MipSlice = static_cast<UINT>(base_mip_lvl);
				uav_desc.Texture2D.PlaneSlice = 0;
				break;
			case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
				uav_desc.Texture2DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
				uav_desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
				uav_desc.Texture2DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
				uav_desc.Texture2DArray.PlaneSlice = 0;
				break;
			case D3D12_UAV_DIMENSION_TEXTURE3D:
				uav_desc.Texture3D.MipSlice = static_cast<UINT>(base_mip_lvl);
				uav_desc.Texture3D.FirstWSlice = static_cast<UINT>(base_arr_layer);
				uav_desc.Texture3D.WSize = static_cast<UINT>(arr_layer_cnt);
				break;
			default:
				throw std::invalid_argument("CreateTextureView(): Unsupported UAV dimension");
			}

			auto handle = ld.univ_alloc.Allocate();
			ld.impl->CreateUnorderedAccessView(tex	, nullptr, &uav_desc, handle.CPU());
			return {
				res.impl,
				std::move(handle),
				View::Type::UnorderedAccess,
				static_cast<std::uint32_t>(base_mip_lvl),
				static_cast<std::uint32_t>(mip_lvl_cnt),
				static_cast<std::uint32_t>(base_arr_layer),
				static_cast<std::uint32_t>(arr_layer_cnt),
				format
			};
		}

		if (is_rtv) {
			D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
				.Format = format,
				.ViewDimension = ExtractRTVDimension(flags)
			};

			switch (rtv_desc.ViewDimension) {
			case D3D12_RTV_DIMENSION_TEXTURE1D:
				rtv_desc.Texture1D.MipSlice = static_cast<UINT>(base_mip_lvl);
				break;
			case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
				rtv_desc.Texture1DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
				rtv_desc.Texture1DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
				rtv_desc.Texture1DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
				break;
			case D3D12_RTV_DIMENSION_TEXTURE2D:
				rtv_desc.Texture2D.MipSlice = static_cast<UINT>(base_mip_lvl);
				rtv_desc.Texture2D.PlaneSlice = 0;
				break;
			case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
				rtv_desc.Texture2DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
				rtv_desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
				rtv_desc.Texture2DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
				rtv_desc.Texture2DArray.PlaneSlice = 0;
				break;
			case D3D12_RTV_DIMENSION_TEXTURE3D:
				rtv_desc.Texture3D.MipSlice = static_cast<UINT>(base_mip_lvl);
				rtv_desc.Texture3D.FirstWSlice = static_cast<UINT>(base_arr_layer);
				rtv_desc.Texture3D.WSize = static_cast<UINT>(arr_layer_cnt);
				break;
			default:
				throw std::invalid_argument("CreateTextureView(): Unsupported RTV dimension");
			}

			auto handle = ld.rtv_alloc.Allocate();
			ld.impl->CreateRenderTargetView(tex, &rtv_desc, handle.CPU());
			return {
				res.impl,
				std::move(handle),
				View::Type::RenderTarget,
				static_cast<std::uint32_t>(base_mip_lvl),
				static_cast<std::uint32_t>(mip_lvl_cnt),
				static_cast<std::uint32_t>(base_arr_layer),
				static_cast<std::uint32_t>(arr_layer_cnt),
				format
			};
		}

		if (is_dsv) {
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
				.Format = format,
				.ViewDimension = ExtractDSVDimension(flags),
				.Flags = ExtractDSVFlags(flags)
			};

			switch (dsv_desc.ViewDimension) {
			case D3D12_DSV_DIMENSION_TEXTURE1D:
				dsv_desc.Texture1D.MipSlice = static_cast<UINT>(base_mip_lvl);
				break;
			case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
				dsv_desc.Texture1DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
				dsv_desc.Texture1DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
				dsv_desc.Texture1DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
				break;
			case D3D12_DSV_DIMENSION_TEXTURE2D:
				dsv_desc.Texture2D.MipSlice = static_cast<UINT>(base_mip_lvl);
				break;
			case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
				dsv_desc.Texture2DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
				dsv_desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
				dsv_desc.Texture2DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
				break;
			default:
				throw std::invalid_argument("CreateTextureView(): Unsupported DSV dimension");
			}

			auto handle = ld.dsv_alloc.Allocate();
			ld.impl->CreateDepthStencilView(tex, &dsv_desc, handle.CPU());
			return {
				res.impl,
				std::move(handle),
				View::Type::DepthStencil,
				static_cast<std::uint32_t>(base_mip_lvl),
				static_cast<std::uint32_t>(mip_lvl_cnt),
				static_cast<std::uint32_t>(base_arr_layer),
				static_cast<std::uint32_t>(arr_layer_cnt),
				format
			};
		}

		throw std::invalid_argument("CreateTextureView(): no valid view type specified in flags");

	}

	Backend::View Backend::CreateBufferView(LogicalDevice& ld, Backend::Resource const& res, std::size_t offset, std::size_t range, ResourceFlags const& flags) {
		
		ID3D12Resource* buf = res.impl->GetResource();
		DXGI_FORMAT format = ExtractFormat(flags);

		bool is_uav = flags.Test(ResourceFlagBits::StorageBuffer) ||
			flags.Test(ResourceFlagBits::StorageTexelBuffer) ||
			flags.Test(ResourceFlagBits::StorageBinding);
		bool is_srv = flags.Test(ResourceFlagBits::TextureBinding); // Buffer SRV uses the same flag.

		if (is_uav && is_srv) {
			throw std::invalid_argument(
				"CreateBufferView(): conflicting flags – cannot create both SRV and UAV.");
		}
		if (!is_uav && !is_srv) {
			// Default to SRV if no explicit view type is given.
			is_srv = true;
		}

		// Allocate a descriptor from the universal heap (supports CBV/SRV/UAV).
		auto handle = ld.univ_alloc.Allocate();

		if (is_srv) {
			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
				.ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
				.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
				.Buffer = { 0u, 0u, 0u, D3D12_BUFFER_SRV_FLAG_NONE }
			};

			if (format == DXGI_FORMAT_UNKNOWN) {
				// Raw buffer view (byte addressable, 32‑bit elements).
				if ((offset % 4) != 0 || (range % 4) != 0) {
					throw std::invalid_argument(
						"CreateBufferView(): for raw buffer SRV, offset and range must be multiples of 4.");
				}
				srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
				srv_desc.Buffer.FirstElement = offset / 4u;
				srv_desc.Buffer.NumElements = static_cast<UINT>(range / 4u);
				srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
			}
			else {
				UINT element_size = BitsPerPixel(format) / 8;
				if (element_size == 0) {
					throw std::invalid_argument(
						"CreateBufferView(): cannot create typed buffer SRV with unknown element size.");
				}
				if ((offset % element_size) != 0 || (range % element_size) != 0) {
					throw std::invalid_argument(
						"CreateBufferView(): offset and range must be aligned to the element size.");
				}
				srv_desc.Format = format;
				srv_desc.Buffer.FirstElement = offset / element_size;
				srv_desc.Buffer.NumElements = static_cast<UINT>(range / element_size);
			}

			ld.impl->CreateShaderResourceView(buf, &srv_desc, handle.CPU());
		}
		else if (is_uav) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
				.ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
				.Buffer = { 0u, 0u, 0u, D3D12_BUFFER_UAV_FLAG_NONE }
			};

			if (format == DXGI_FORMAT_UNKNOWN) {
				// Raw buffer UAV.
				if ((offset % 4) != 0 || (range % 4) != 0) {
					throw std::invalid_argument(
						"CreateBufferView(): for raw buffer UAV, offset and range must be multiples of 4.");
				}
				uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
				uav_desc.Buffer.FirstElement = offset / 4u;
				uav_desc.Buffer.NumElements = static_cast<UINT>(range / 4u);
				uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
			}
			else {
				UINT element_size = BitsPerPixel(format) / 8;
				if (element_size == 0) {
					throw std::invalid_argument(
						"CreateBufferView(): cannot create typed buffer UAV with unknown element size.");
				}
				if ((offset % element_size) != 0 || (range % element_size) != 0) {
					throw std::invalid_argument(
						"CreateBufferView(): offset and range must be aligned to the element size.");
				}
				uav_desc.Format = format;
				uav_desc.Buffer.FirstElement = offset / element_size;
				uav_desc.Buffer.NumElements = static_cast<UINT>(range / element_size);
			}

			ld.impl->CreateUnorderedAccessView(buf, nullptr, &uav_desc, handle.CPU());
		}

		return {
			res.impl,
			std::move(handle),
			is_srv ? View::Type::ShaderResource : View::Type::UnorderedAccess,
			0u,
			1u,
			0u,
			1u,
			format
		};
		
		
	}

	Backend::Sampler Backend::CreateSampler(Backend::LogicalDevice& ld, SamplerDescriptor const& descriptor) {

		D3D12_SAMPLER_DESC sampler_desc = {
			.Filter = BuildFilter(
				descriptor.mag_filter,
				descriptor.min_filter,
				descriptor.mipmap_filter,
				descriptor.max_anisotropy,
				descriptor.compare_function
			),
			.AddressU = MapAddressMode(descriptor.address_mode_u),
			.AddressV = MapAddressMode(descriptor.address_mode_v),
			.AddressW = MapAddressMode(descriptor.address_mode_w),
			.MaxAnisotropy = std::clamp(descriptor.max_anisotropy, static_cast<std::uint8_t>(1u), static_cast<std::uint8_t>(16u)),
			.ComparisonFunc = MapCompareFunction(descriptor.compare_function),
			.MinLOD = descriptor.min_lod,
			.MaxLOD = descriptor.max_lod
		};

		auto handle = ld.sampler_alloc.Allocate();
		ld.impl->CreateSampler(&sampler_desc, handle.CPU());
		return handle;
	}

	Backend::Pipeline Backend::CreateGraphicsPipeline(Backend::LogicalDevice const& ld, GraphicsPipelineDescriptor const& desc) {
		slang::TargetDesc target{
			.format = SLANG_DXIL,
			.profile = QueryHighestSMLevel(ld)
		};
		DXGI_ADAPTER_DESC1 adapter_desc{};
		ThrowIfFailed(ld.phys_dev->GetDesc1(&adapter_desc));
		auto driver = QueryDXDriverVersion(adapter_desc);
		auto cache_tag = std::format(
			"d3d12-{:04x}-{:04x}-{}.{}.{}.{}",
			adapter_desc.VendorId,
			adapter_desc.DeviceId,
			driver[0], driver[1], driver[2], driver[3]
		);
		shader::SlangProgram program(target, desc.program, cache_tag);

		Backend::Pipeline pipeline;
		pipeline.bindings = MakePipelineBindingMetadata(program.Interface());
		pipeline.root_signature = CreateRootSignature(ld, program.Interface());
		pipeline.primitive_topology = MapTopology(desc.primitive.topology);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC native{};
		native.pRootSignature = pipeline.root_signature.Get();
		for (auto const& entry : program.EntryPoints()) {
			D3D12_SHADER_BYTECODE bytecode{ entry.code.data(), entry.code.size() };
			switch (entry.stage) {
			case PipelineStage::Vertex: native.VS = bytecode; break;
			case PipelineStage::Fragment: native.PS = bytecode; break;
			case PipelineStage::TessellationControl: native.HS = bytecode; break;
			case PipelineStage::TessellationEvaluation: native.DS = bytecode; break;
			case PipelineStage::Geometry: native.GS = bytecode; break;
			case PipelineStage::Task:
			case PipelineStage::Mesh:
				throw std::invalid_argument("Mesh pipelines require the D3D12 pipeline-state stream path");
			default:
				throw std::invalid_argument("A compute entry point cannot be used by a graphics pipeline");
			}
		}
		if (!native.VS.pShaderBytecode) {
			throw std::invalid_argument("D3D12 graphics pipeline has no vertex entry point");
		}

		std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
		input_elements.reserve(desc.vertex.attributes.size());
		for (auto const& attribute : desc.vertex.attributes) {
			auto reflected = std::ranges::find(program.Interface().vertex_inputs, attribute.location, &SlangPipelineVertexInput::location);
			if (reflected == program.Interface().vertex_inputs.end()) {
				throw std::invalid_argument(std::format("No reflected vertex input at location {}", attribute.location));
			}
			auto buffer = std::ranges::find(desc.vertex.buffers, attribute.slot, &VertexBufferLayout::slot);
			if (buffer == desc.vertex.buffers.end()) {
				throw std::invalid_argument(std::format("No vertex buffer layout for slot {}", attribute.slot));
			}
			input_elements.push_back(
				{
					.SemanticName = reflected->semantic_name.c_str(),
					.SemanticIndex = reflected->semantic_index,
					.Format = MapFormat(attribute.format),
					.InputSlot = attribute.slot,
					.AlignedByteOffset = attribute.offset,
					.InputSlotClass = buffer->input_rate == VertexInputRate::Vertex
						? D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
						: D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
					.InstanceDataStepRate = buffer->input_rate == VertexInputRate::Vertex ? 0u : 1u
				}
			);
		}
		native.InputLayout = { input_elements.data(), static_cast<UINT>(input_elements.size()) };
		native.PrimitiveTopologyType = MapTopologyType(desc.primitive.topology);
		if (desc.primitive.strip_index_format) {
			if (
				desc.primitive.topology != PrimitiveTopology::LineStrip &&
				desc.primitive.topology != PrimitiveTopology::TriangleStrip
			) {
				throw std::invalid_argument("D3D12 strip index format requires a strip topology");
			}
			native.IBStripCutValue =
				*desc.primitive.strip_index_format == IndexFormat::Uint16
					? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF
					: D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
		}

		native.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		native.RasterizerState.FrontCounterClockwise = desc.rasterization.front_face == FrontFace::CounterClockwise;
		native.RasterizerState.CullMode =
			desc.rasterization.cull_mode == CullMode::Front ? D3D12_CULL_MODE_FRONT :
			desc.rasterization.cull_mode == CullMode::Back ? D3D12_CULL_MODE_BACK :
			D3D12_CULL_MODE_NONE;
		native.RasterizerState.DepthBias = desc.rasterization.depth_bias.constant;
		native.RasterizerState.SlopeScaledDepthBias = desc.rasterization.depth_bias.slope_scale;
		native.RasterizerState.DepthBiasClamp = desc.rasterization.depth_bias.clamp;

		native.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		native.BlendState.AlphaToCoverageEnable = desc.multisample.alpha_to_coverage_enabled;
		if (desc.color_targets.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) {
			throw std::invalid_argument("D3D12 supports at most eight color targets");
		}
		native.NumRenderTargets = static_cast<UINT>(desc.color_targets.size());
		for (std::size_t index = 0; index < desc.color_targets.size(); ++index) {
			auto const& target_state = desc.color_targets[index];
			native.RTVFormats[index] = MapFormat(target_state.format);
			auto& target_blend = native.BlendState.RenderTarget[index];
			target_blend.RenderTargetWriteMask = static_cast<UINT8>(target_state.write_mask);
			if (target_state.blend) {
				auto const& blend = *target_state.blend;
				target_blend.BlendEnable = true;
				target_blend.SrcBlend = MapBlendFactor(blend.color.source_factor);
				target_blend.DestBlend = MapBlendFactor(blend.color.destination_factor);
				target_blend.BlendOp = MapBlendOperation(blend.color.operation);
				target_blend.SrcBlendAlpha = MapBlendFactor(blend.alpha.source_factor);
				target_blend.DestBlendAlpha = MapBlendFactor(blend.alpha.destination_factor);
				target_blend.BlendOpAlpha = MapBlendOperation(blend.alpha.operation);
			}
		}

		native.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		native.DepthStencilState.DepthEnable = false;
		native.DepthStencilState.StencilEnable = false;
		if (desc.depth_stencil) {
			auto const& state = *desc.depth_stencil;
			native.DSVFormat = MapFormat(state.format);
			native.DepthStencilState.DepthEnable = state.depth_test_enabled;
			native.DepthStencilState.DepthWriteMask = state.depth_write_enabled
				? D3D12_DEPTH_WRITE_MASK_ALL
				: D3D12_DEPTH_WRITE_MASK_ZERO;
			native.DepthStencilState.DepthFunc = MapPipelineCompare(state.depth_compare);
			native.DepthStencilState.StencilEnable = state.stencil_enabled;
			native.DepthStencilState.StencilReadMask = static_cast<UINT8>(state.stencil_read_mask);
			native.DepthStencilState.StencilWriteMask = static_cast<UINT8>(state.stencil_write_mask);
			native.DepthStencilState.FrontFace = MapStencilFace(state.stencil_front);
			native.DepthStencilState.BackFace = MapStencilFace(state.stencil_back);
		}
		native.SampleMask = desc.multisample.mask;
		native.SampleDesc = { MapSampleCount(desc.multisample.sample_count), 0 };

		boost::hash2::xxhash_64 pso_hash;
		constexpr std::uint32_t pso_schema = 1;
		pso_hash.update(&pso_schema, sizeof(pso_schema));
		auto root_key = RootSignatureCacheKey(program.Interface());
		pso_hash.update(root_key.data(), root_key.size());
		for (auto const& entry : program.EntryPoints()) {
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
		auto pso_path = cache::GetCacheFilePath(std::format("d3d12-graphics-pso-{:016x}.bin", pso_hash.result()));
		auto cached_pso = cache::ReadFile(pso_path);
		native.CachedPSO = { cached_pso.data(), cached_pso.size() };

		auto creation_result = ld.impl->CreateGraphicsPipelineState(
			&native,
			IID_PPV_ARGS(&pipeline.impl)
		);
		if (FAILED(creation_result) && !cached_pso.empty()) {
			native.CachedPSO = {};
			cached_pso.clear();
			creation_result = ld.impl->CreateGraphicsPipelineState(
				&native,
				IID_PPV_ARGS(&pipeline.impl)
			);
		}
		ThrowIfFailed(creation_result);
		if (cached_pso.empty()) {
			Microsoft::WRL::ComPtr<ID3DBlob> blob;
			ThrowIfFailed(pipeline.impl->GetCachedBlob(&blob));
			auto begin = static_cast<std::byte const*>(blob->GetBufferPointer());
			if (!cache::WriteFileAtomically(
				pso_path,
				std::span<std::byte const>(begin, blob->GetBufferSize())
			)) {
				throw std::runtime_error("Failed to write D3D12 graphics-pipeline cache");
			}
		}
		return pipeline;
	}

	Backend::Pipeline Backend::CreateComputePipeline(
		Backend::LogicalDevice const& ld,
		ComputePipelineDescriptor const& descriptor
	) {
		slang::TargetDesc target{
			.format = SLANG_DXIL,
			.profile = QueryHighestSMLevel(ld)
		};
		DXGI_ADAPTER_DESC1 adapter_desc{};
		ThrowIfFailed(ld.phys_dev->GetDesc1(&adapter_desc));
		auto driver = QueryDXDriverVersion(adapter_desc);
		auto cache_tag = std::format(
			"d3d12-compute-{:04x}-{:04x}-{}.{}.{}.{}",
			adapter_desc.VendorId,
			adapter_desc.DeviceId,
			driver[0], driver[1], driver[2], driver[3]
		);
		shader::SlangProgram program(target, descriptor.program, cache_tag);
		Backend::Pipeline pipeline;
		pipeline.compute = true;
		pipeline.bindings = MakePipelineBindingMetadata(program.Interface());
		pipeline.root_signature = CreateRootSignature(ld, program.Interface());

		D3D12_COMPUTE_PIPELINE_STATE_DESC native{};
		native.pRootSignature = pipeline.root_signature.Get();
		for (auto const& entry : program.EntryPoints()) {
			if (entry.stage != PipelineStage::Compute) {
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
		auto root_key = RootSignatureCacheKey(program.Interface());
		pso_hash.update(root_key.data(), root_key.size());
		for (auto const& entry : program.EntryPoints()) {
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
		auto creation_result = ld.impl->CreateComputePipelineState(
			&native,
			IID_PPV_ARGS(&pipeline.impl)
		);
		if (FAILED(creation_result) && !cached_pso.empty()) {
			native.CachedPSO = {};
			cached_pso.clear();
			creation_result = ld.impl->CreateComputePipelineState(
				&native,
				IID_PPV_ARGS(&pipeline.impl)
			);
		}
		ThrowIfFailed(creation_result);
		if (cached_pso.empty()) {
			Microsoft::WRL::ComPtr<ID3DBlob> blob;
			ThrowIfFailed(pipeline.impl->GetCachedBlob(&blob));
			auto begin = static_cast<std::byte const*>(blob->GetBufferPointer());
			if (!cache::WriteFileAtomically(
				pso_path,
				std::span<std::byte const>(begin, blob->GetBufferSize())
			)) {
				throw std::runtime_error("Failed to write D3D12 compute-pipeline cache");
			}
		}
		return pipeline;
	}

	Backend::PipelineResourceGroup Backend::CreatePipelineResourceGroup(
		Backend::LogicalDevice const& ld,
		Backend::Pipeline const& pipeline,
		std::uint32_t space,
		std::span<NativePipelineResourceBinding<Backend> const> bindings
	) {
		auto native = MakePipelineResourceGroup<Backend>(pipeline.bindings, space, bindings);
		PipelineResourceGroup result;
		result.space = space;
		result.root_signature = pipeline.root_signature;
		result.resource_heap = ld.univ_alloc.GetHeap();
		result.sampler_heap = ld.sampler_alloc.GetHeap();
		for (std::uint32_t root_parameter = 0u;
			root_parameter < pipeline.bindings.size();
			++root_parameter) {
			auto const& layout = pipeline.bindings[root_parameter];
			if (layout.space != space) continue;
			bool sampler = layout.flags.Test(ResourceFlagBits::SamplerBinding);
			auto descriptors = sampler ?
				ld.sampler_alloc.Allocate(layout.count) :
				ld.univ_alloc.Allocate(layout.count);
			for (std::uint32_t element = 0u; element < layout.count; ++element) {
				typename NativePipelineResourceGroup<Backend>::Binding const* source = nullptr;
				for (auto const& entry : native.bindings) {
					if (entry.slot == layout.slot && entry.array_element == element) {
						source = &entry;
						break;
					}
				}
				if (!source) {
					throw std::invalid_argument("D3D12 resource groups require every descriptor array element");
				}
				auto destination = descriptors.CPU(element);
				if (auto view_binding = std::get_if<NativePipelineViewBinding<Backend>>(&source->value)) {
					ld.impl->CopyDescriptorsSimple(
						1u, destination, view_binding->get().CPU(),
						D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
					);
				}
				else if (auto sampler_binding = std::get_if<NativePipelineSamplerBinding<Backend>>(&source->value)) {
					ld.impl->CopyDescriptorsSimple(
						1u, destination, sampler_binding->get().CPU(),
						D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
					);
				}
				else if (auto buffer_binding = std::get_if<NativePipelineBufferBinding<Backend>>(&source->value)) {
					auto resource = buffer_binding->impl.get().impl->GetResource();
					auto resource_size = static_cast<std::size_t>(resource->GetDesc().Width);
					if (buffer_binding->offset > resource_size) {
						throw std::out_of_range("D3D12 pipeline buffer offset exceeds the resource");
					}
					auto size = buffer_binding->size == PipelineWholeBuffer ?
						resource_size - buffer_binding->offset : buffer_binding->size;
					if (size == 0u || size > resource_size - buffer_binding->offset) {
						throw std::out_of_range("D3D12 pipeline buffer range exceeds the resource");
					}
					if (layout.flags.Test(ResourceFlagBits::UniformBuffer)) {
						if (buffer_binding->offset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT != 0u) {
							throw std::invalid_argument("D3D12 constant buffer offset must be 256-byte aligned");
						}
						if (size > (std::numeric_limits<std::size_t>::max)() -
							(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u)) {
							throw std::out_of_range("D3D12 constant buffer range cannot be represented");
						}
						auto aligned_size =
							(size + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u) &
							~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u);
						if (aligned_size > resource_size - buffer_binding->offset ||
							aligned_size > (std::numeric_limits<UINT>::max)()) {
							throw std::out_of_range("D3D12 constant buffer range cannot be represented");
						}
						D3D12_CONSTANT_BUFFER_VIEW_DESC descriptor{
							.BufferLocation = resource->GetGPUVirtualAddress() + buffer_binding->offset,
							.SizeInBytes = static_cast<UINT>(aligned_size)
						};
						ld.impl->CreateConstantBufferView(&descriptor, destination);
					}
					else {
						if (buffer_binding->offset % sizeof(std::uint32_t) != 0u ||
							size % sizeof(std::uint32_t) != 0u ||
							size / sizeof(std::uint32_t) > (std::numeric_limits<UINT>::max)()) {
							throw std::invalid_argument("D3D12 storage buffer range is invalid");
						}
						D3D12_UNORDERED_ACCESS_VIEW_DESC descriptor{
							.Format = DXGI_FORMAT_R32_TYPELESS,
							.ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
							.Buffer = {
								.FirstElement = buffer_binding->offset / sizeof(std::uint32_t),
								.NumElements = static_cast<UINT>(size / sizeof(std::uint32_t)),
								.StructureByteStride = 0u,
								.CounterOffsetInBytes = 0u,
								.Flags = D3D12_BUFFER_UAV_FLAG_RAW
							}
						};
						ld.impl->CreateUnorderedAccessView(resource, nullptr, &descriptor, destination);
					}
				}
				else {
					throw std::invalid_argument("Unsupported D3D12 pipeline resource binding");
				}
			}
			result.tables.emplace_back(root_parameter, std::move(descriptors));
		}
		return result;
	}

	Backend::Scheduler Backend::CreateScheduler(
		LogicalDevice const& ld,
		SchedulerDescriptor const& descriptor
	) {
		using Flag = SchedulerFlagBits;
		Backend::Scheduler::QueueCollection queues;
		if (descriptor.flags.Test(Flag::Graphics)) {
			queues.graphics = CreateSchedulerQueue(ld, D3D12_COMMAND_LIST_TYPE_DIRECT, descriptor.priority);
		}
		if (descriptor.flags.Test(Flag::Compute)) {
			queues.compute = CreateSchedulerQueue(ld, D3D12_COMMAND_LIST_TYPE_COMPUTE, descriptor.priority);
		}
		if (descriptor.flags.Test(Flag::Copy)) {
			queues.copy = CreateSchedulerQueue(ld, D3D12_COMMAND_LIST_TYPE_COPY, descriptor.priority);
		}
		if (!queues.graphics && !queues.compute && !queues.copy) {
			throw std::invalid_argument("CreateScheduler(): no execution capability was requested");
		}
		return {
			std::make_shared<Backend::Scheduler::Implementation>(
				queues,
				ld.phys_dev,
				ld.presentation_cache,
				ld.completion_service
			)
		};
	}

	Backend::CommandGraph Backend::CreateCommandGraph(
		execution::CommandGraphDescriptor const& descriptor
	) {
		return execution::MakeCommandGraph<Backend>(descriptor);
	}

}
#endif // defined(_WIN32)
