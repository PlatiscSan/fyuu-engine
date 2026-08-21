module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <vector>

#include <cstdint>

#include <optional>
#include <variant>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <Metal/Metal.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_data;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline;

namespace fyuu_rhi::metal {

	struct Instance {
	};

	struct PhysicalDevice {
		NS::SharedPtr<MTL::Device> impl;
	};

	struct LogicalDevice {
		NS::SharedPtr<MTL::Device> impl;
	};

	struct CompletionToken {
		std::vector<NS::SharedPtr<MTL::CommandBuffer>> command_buffers;
		std::exception_ptr error;
		bool stopped = false;
	};

	struct CommandSchedulerContext {
		NS::SharedPtr<MTL::Device> device;
		NS::SharedPtr<MTL::CommandQueue> queue;
	};

	/// A buffer view is just a (buffer, offset, size) window; Metal binds buffers
	/// with an offset at encode time and has no separate buffer-view object.
	struct BufferView {
		NS::SharedPtr<MTL::Buffer> buffer;
		std::size_t offset;
		std::size_t size;
	};

	struct Resource {
		std::variant<NS::SharedPtr<MTL::Buffer>, NS::SharedPtr<MTL::Texture>> impl;
	};

	/// A texture view is itself an MTL::Texture created from the parent texture.
	struct View {
		std::variant<BufferView, NS::SharedPtr<MTL::Texture>> impl;
	};

	struct Sampler {
		NS::SharedPtr<MTL::SamplerState> impl;
	};

	struct PipelineBinding {
		pipeline::BindingMetadata metadata;
		std::uint32_t visibility;
	};

	struct Pipeline {
		NS::SharedPtr<MTL::Device> device;
		std::variant<
			NS::SharedPtr<MTL::RenderPipelineState>,
			NS::SharedPtr<MTL::ComputePipelineState>
		> impl;
		NS::SharedPtr<MTL::DepthStencilState> depth_stencil;
		MTL::PrimitiveType primitive_type;
		MTL::Winding front_face;
		MTL::CullMode cull_mode;
		pipeline::DepthBiasState depth_bias;
		std::vector<PipelineBinding> bindings;
	};

	struct PipelineResourceGroup {
		struct Binding {
			std::uint32_t slot;
			std::uint32_t visibility;
			NS::SharedPtr<MTL::Buffer> buffer;
			std::size_t buffer_offset;
			std::size_t buffer_size;
			NS::SharedPtr<MTL::Texture> texture;
			NS::SharedPtr<MTL::SamplerState> sampler;
		};

		std::uint32_t space;
		std::vector<Binding> bindings;
	};

} // namespace fyuu_rhi::metal
#endif // defined(__APPLE__)
