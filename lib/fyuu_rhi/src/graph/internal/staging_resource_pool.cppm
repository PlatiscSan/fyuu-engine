module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:staging_resource_pool;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :resource_types;

namespace fyuu_rhi::execution {
	enum class StagingResourceKind : std::uint8_t {
		Upload,
		Readback
	};

	template <class Backend> class StagingResourcePool {
		struct Entry {
			StagingResourceKind kind;
			Resource<Backend> resource;
		};

		static constexpr std::size_t MinimumAllocationSize = 4u * 1024u;
		static constexpr std::size_t MaximumRetainedSize = 64u * 1024u * 1024u;
		static constexpr std::size_t MaximumEntrySize = 16u * 1024u * 1024u;

		std::deque<Entry> m_entries;
		std::size_t m_retained_size = 0u;
		std::mutex m_mutex;

		[[nodiscard]] static std::size_t AllocationSize(std::size_t size) {
			auto requested = std::max(size, MinimumAllocationSize);
			if (requested > (std::numeric_limits<std::size_t>::max)() / 2u + 1u) {
				return requested;
			}
			return std::bit_ceil(requested);
		}

		[[nodiscard]] static ResourceFlags Flags(StagingResourceKind kind) {
			ResourceFlags flags;
			if (kind == StagingResourceKind::Upload) {
				flags.Set(ResourceFlagBits::HostVisible);
				flags.Set(ResourceFlagBits::CopySRC);
			}
			else {
				flags.Set(ResourceFlagBits::DeviceReadback);
				flags.Set(ResourceFlagBits::CopyDST);
			}
			return flags;
		}

	public:
		template <class Create>
		Resource<Backend> Acquire(
			std::size_t size,
			StagingResourceKind kind,
			Create&& create
		) {
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				auto selected = m_entries.end();
				for (auto iterator = m_entries.begin(); iterator != m_entries.end(); ++iterator) {
					if (iterator->kind != kind || iterator->resource.Size() < size) {
						continue;
					}
					if (selected == m_entries.end() ||
						iterator->resource.Size() < selected->resource.Size()) {
						selected = iterator;
					}
				}
				if (selected != m_entries.end()) {
					auto resource = std::move(selected->resource);
					m_retained_size -= resource.Size();
					m_entries.erase(selected);
					return resource;
				}
			}
			auto allocation_size = AllocationSize(size);
			return std::forward<Create>(create)(allocation_size, Flags(kind));
		}

		bool Release(Resource<Backend>& resource, StagingResourceKind kind) {
			if (resource.ID() == 0u || resource.Size() > MaximumEntrySize) {
				return false;
			}
			std::unique_lock<std::mutex> lock(m_mutex);
			if (resource.Size() > MaximumRetainedSize - m_retained_size) {
				return false;
			}
			m_entries.emplace_back(Entry{ kind, std::move(resource) });
			m_retained_size += m_entries.back().resource.Size();
			return true;
		}
	};

}
