module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include <cstdint>

#include <span>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:resource;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.atomic_flags;
import :view;

export namespace fyuu_rhi {
	namespace execution {
		template <class NativeCommandSchedulerContext>
		struct ExecuteCommands;
	}

	/**
	 * @brief Orthogonal creation capabilities for buffers, textures, and views.
	 *
	 * A resource must declare every operation it will participate in. Select one
	 * memory policy, one texture dimension for textures, one sample count, and one
	 * format where required. View flags describe which views may be created later.
	 */
	enum class ResourceFlagBits : std::uint32_t {
		CopySRC,
		CopyDST,

		UniformTexelBuffer,
		StorageTexelBuffer,
		UniformBuffer,
		StorageBuffer,
		IndexBuffer,
		VertexBuffer,
		IndirectBuffer,

		TextureBinding,
		SamplerBinding,
		StorageBinding,
		RenderAttachment,
		TransientAttachment,
		StorageAttachment,

		Texture1D,
		Texture2D,
		Texture3D,

		TextureView1D,
		TextureView2D,
		TextureView2DArray,
		TextureViewCube,
		TextureViewCubeArray,
		TextureView3D,

		TextureViewAspectAll,
		TextureViewAspectStencilOnly,
		TextureViewAspectDepthOnly,

		TextureViewAspectPlane0Only,
		TextureViewAspectPlane1Only,
		TextureViewAspectPlane2Only,

		UndedicatedAllocation,
		DedicatedAllocation,

		AllocationWithinBudget,
		AllocationAtUpperAddress,
		AllocationAliasAllowed,

		MinOffsetAllocation,
		BestFitAllocation,
		FirstFitAllocation,

		DeviceLocal,
		HostVisible,
		DeviceReadback,

		Sample1,
		Sample2,
		Sample4,
		Sample8,
		Sample16,
		Sample32,
		Sample64,

		// 8-bit per component
		R8Unorm,
		R8Snorm,
		R8Uint,
		R8Sint,

		// 16-bit per component (2-channel)
		R8G8Unorm,
		R8G8Snorm,
		R8G8Uint,
		R8G8Sint,

		// 32-bit per component (4-channel)
		R8G8B8A8Unorm,
		R8G8B8A8Snorm,
		R8G8B8A8Uint,
		R8G8B8A8Sint,
		R8G8B8A8Srgb,
		B8G8R8A8Srgb,

		// 16-bit per component (1-channel)
		R16Unorm,
		R16Snorm,
		R16Uint,
		R16Sint,
		R16Float,

		// 16-bit per component (2-channel)
		R16G16Unorm,
		R16G16Snorm,
		R16G16Uint,
		R16G16Sint,
		R16G16Float,

		// 16-bit per component (4-channel)
		R16G16B16A16Unorm,
		R16G16B16A16Snorm,
		R16G16B16A16Uint,
		R16G16B16A16Sint,
		R16G16B16A16Float,

		// 32-bit per component (1-channel)
		R32Uint,
		R32Sint,
		R32Float,

		// 32-bit per component (2-channel)
		R32G32Uint,
		R32G32Sint,
		R32G32Float,

		// 32-bit per component (4-channel)
		R32G32B32A32Uint,
		R32G32B32A32Sint,
		R32G32B32A32Float,

		// Packed formats
		R10G10B10A2Unorm,     // DXGI_R10G10B10A2_UNORM → Vulkan A2R10G10B10_UNORM_PACK32
		R10G10B10A2Uint,
		R11G11B10Float,       // DXGI_R11G11B10_FLOAT → Vulkan B10G11R11_UFLOAT_PACK32
		R9G9B9E5SharedExp,    // DXGI_R9G9B9E5_SHAREDEXP → Vulkan E5B9G9R9_UFLOAT_PACK32

		// Depth/stencil
		D16Unorm,
		D24UnormS8Uint,
		D32Float,
		D32FloatS8X24Uint,

		// BC compressed formats
		Bc1Unorm,             // BC1_UNORM (RGBA, 1-bit alpha)
		Bc1UnormSrgb,         // BC1_UNORM_SRGB
		Bc2Unorm,
		Bc2UnormSrgb,
		Bc3Unorm,
		Bc3UnormSrgb,
		Bc4Unorm,
		Bc4Snorm,
		Bc5Unorm,
		Bc5Snorm,
		Bc6HUfloat,           // BC6H_UF16
		Bc6HSfloat,           // BC6H_SF16
		Bc7Unorm,
		Bc7UnormSrgb,

		Count
	};

	/// Thread-safe flag set used to assemble ResourceFlagBits creation contracts.
	using ResourceFlags = plastic::concurrency::AtomicFlags<ResourceFlagBits>;

	/// Byte interval within a buffer.
	struct ResourceDataRange {
		std::size_t offset = 0u;
		std::size_t size = 0u;
	};

	/// Immutable dimensions recorded for a texture resource.
	struct ResourceTextureExtent {
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
		std::uint32_t depth_or_array_layers = 0u;
		std::uint32_t mip_levels = 0u;
	};

	class ResourceMapScope {
	private:
		void* m_context = nullptr;
		std::byte* m_data = nullptr;
		std::size_t m_offset = 0u;
		std::size_t m_size = 0u;
		bool m_writable = false;
		void (*m_unmap)(void*, ResourceDataRange, bool) noexcept = nullptr;

		template <class NativeResource>
		friend struct MapResource;

		ResourceMapScope(
			void* context,
			std::byte* data,
			std::size_t offset,
			std::size_t size,
			bool writable,
			void (*unmap)(void*, ResourceDataRange, bool) noexcept
		) noexcept
			: m_context(context),
			m_data(data),
			m_offset(offset),
			m_size(size),
			m_writable(writable),
			m_unmap(unmap) {
		}

	public:
		ResourceMapScope(ResourceMapScope const&) = delete;
		ResourceMapScope& operator=(ResourceMapScope const&) = delete;

		ResourceMapScope(ResourceMapScope&& other) noexcept;
		ResourceMapScope& operator=(ResourceMapScope&& other) noexcept;

		~ResourceMapScope() noexcept;

		/// Ends the mapping immediately. Calling Reset() again has no effect.
		void Reset() noexcept;

		/// Returns a read-only view of the mapped byte interval.
		std::span<std::byte const> Read() noexcept {
			return { m_data, m_size };
		}

		/**
		 * @brief Copies bytes into the mapped interval.
		 * @param data Bytes copied to the beginning of the mapped interval.
		 * @throws std::logic_error if the resource was not created as HostVisible.
		 * @throws std::out_of_range if @p data is larger than the mapped interval.
		 */
		void Write(std::span<std::byte const> data);
	};

	/// Host/buffer layout used by buffer-to-texture and texture-to-buffer copies.
	struct TextureDataLayout {
		std::size_t offset = 0u;
		std::uint32_t bytes_per_row = 0u;
		std::uint32_t rows_per_image = 0u;
	};

	/// Texture subresource and texel box used by copy commands.
	struct TextureRegion {
		std::uint32_t mip_level = 0u;
		std::uint32_t base_array_layer = 0u;
		std::uint32_t array_layer_count = 1u;
		std::uint32_t offset_x = 0u;
		std::uint32_t offset_y = 0u;
		std::uint32_t offset_z = 0u;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
		std::uint32_t depth = 1u;
	};

	/**
	 * @brief Move-only owner of either a buffer or texture.
	 *
	 * Resources create their own views so backend and logical-device identity can
	 * be validated without exposing native handles. During command execution a
	 * Resource is moved into CommandGraphBindings and returned at completion.
	 */
	class Resource {
	public:
		using UniqueHandle = std::unique_ptr<
			struct ResourceImplementation,
			void(*)(struct ResourceImplementation*)
		>;

	private:
		template <class Native>
		friend struct CreatePipelineResourceGroup;
		template <class Native>
		friend struct execution::ExecuteCommands;

		UniqueHandle m_impl;

	public:
		Resource() noexcept
			: m_impl(nullptr, nullptr) {
		}

		explicit Resource(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		/// Creates a typed view over the byte range [offset, offset + range).
		View CreateBufferView(
			std::size_t offset,
			std::size_t range,
			ResourceFlags const& flags
		);

		/// Creates a view over the selected mip and array-layer ranges.
		View CreateTextureView(
			std::size_t base_mip_lvl,
			std::size_t mip_lvl_cnt,
			std::size_t base_arr_layer,
			std::size_t arr_layer_cnt,
			ResourceFlags const& flags
		);

		/// Returns the byte size; calling this on a texture is an error.
		std::size_t GetBufferSize() const;

		/// Returns the immutable creation flags.
		ResourceFlags GetFlags() const noexcept;

		/// Returns texture dimensions; calling this on a buffer is an error.
		ResourceTextureExtent GetTextureExtent() const;

		/**
		 * @brief Maps a host-visible or readback buffer interval.
		 *
		 * DeviceReadback mappings expose Read(), while HostVisible mappings expose
		 * Write(). GPU work using the interval must already be complete. Textures
		 * must first be copied into a buffer. The returned scope owns the mapping
		 * and automatically unmaps it on destruction.
		 */
		ResourceMapScope Map(ResourceDataRange range);

	};

} // namespace fyuu_rhi
