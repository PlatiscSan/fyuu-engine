module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <concepts>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:resource;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource_types;
namespace fyuu_rhi::execution {
	template <class Backend, class Receiver> class CommandGraphBindings;
	template <class Backend, class Receiver> class ResourceMapOperationState;
	template <class Backend, class Receiver> class ResourceUnmapOperationState;
	template <class Backend> class AbandonedResourceMapping;
}
namespace fyuu_rhi {
	export template <class Backend> class Resource {
	public:
		using Implementation = typename Backend::Resource;

	private:
		[[nodiscard]] static std::uint64_t NextID() noexcept {
			static std::atomic_uint64_t next = 1u;
			return next.fetch_add(1u, std::memory_order::relaxed);
		}

		Implementation m_impl;
		std::uint64_t m_id = 0u;
		std::size_t m_size = 0u;
		ResourceTextureExtent m_texture_extent;
		ResourceFlags m_flags;

	public:
		template <class ResType>
		class LogicalDevicePassKey {
			static_assert(std::same_as<ResType, Resource> || std::same_as<ResType, Resource const>,
				"ResType must be Resource or const Resource");

			template <class U> friend class LogicalDevice;
			template <class U, class Receiver> friend class execution::CommandGraphBindings;
			template <class U, class Receiver>
			friend class execution::ResourceMapOperationState;
			template <class U, class Receiver>
			friend class execution::ResourceUnmapOperationState;
			template <class U> friend class execution::AbandonedResourceMapping;

			ResType* m_res;

			template <class Self>
			decltype(auto) GetImplementation(this Self&& self) noexcept {
				return (self.m_res->m_impl);
			}

		public:
			explicit LogicalDevicePassKey(ResType* res) noexcept : m_res(res) {}
		};

		template <std::convertible_to<Implementation> I>
		Resource(I&& impl)
			: m_impl(std::forward<I>(impl)),
			m_id(NextID()) {

		}

		template <std::convertible_to<Implementation> I>
		Resource(
			I&& impl,
			std::size_t size,
			ResourceFlags const& flags,
			ResourceTextureExtent const& texture_extent = {}
		) : m_impl(std::forward<I>(impl)),
			m_id(NextID()),
			m_size(size),
			m_texture_extent(texture_extent),
			m_flags(flags) {

		}

		Resource(Resource const&) = delete;
		Resource& operator=(Resource const&) = delete;
		Resource(Resource&& other) noexcept
			: m_impl(std::move(other.m_impl)),
			m_id(std::exchange(other.m_id, 0u)),
			m_size(std::exchange(other.m_size, 0u)),
			m_texture_extent(std::exchange(other.m_texture_extent, ResourceTextureExtent{})),
			m_flags(std::move(other.m_flags)) {

		}
		Resource& operator=(Resource&& other) noexcept {
			m_impl = std::move(other.m_impl);
			m_id = std::exchange(other.m_id, 0u);
			m_size = std::exchange(other.m_size, 0u);
			m_texture_extent = std::exchange(other.m_texture_extent, ResourceTextureExtent{});
			m_flags = std::move(other.m_flags);
			return *this;
		}
		~Resource() noexcept = default;

		[[nodiscard]] std::uint64_t ID() const noexcept {
			return m_id;
		}

		[[nodiscard]] std::size_t Size() const noexcept {
			return m_size;
		}

		[[nodiscard]] ResourceTextureExtent const& TextureExtent() const noexcept {
			return m_texture_extent;
		}

		[[nodiscard]] ResourceFlags const& Flags() const noexcept {
			return m_flags;
		}

		template <class Self>
		auto GetLogicalDevicePassKey(this Self&& self) noexcept {
			using ResType = std::remove_reference_t<Self>;
			return LogicalDevicePassKey<ResType>{ &self };
		}

	};

}
