module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <limits>

#include <cstdint>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:sampler;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;

export namespace fyuu_rhi {
	namespace execution {
		template <class NativeCommandSchedulerContext>
		struct ExecuteCommands;
	}

	enum class AddressMode : std::uint8_t {
		Unknown = 0,
		ClampToEdge,
		Repeat,
		MirroredRepeat,
	};

	enum class FilterMode : std::uint8_t {
		Unknown = 0,
		Nearest,
		Linear,
	};

	enum class MipmapFilterMode : std::uint8_t {
		Unknown = 0,
		Nearest,
		Linear,
	};

	enum class CompareFunction : std::uint8_t {
		Unknown = 0,
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always,
	};

	struct SamplerDescriptor {
		AddressMode address_mode_u = AddressMode::Unknown;
		AddressMode address_mode_v = AddressMode::Unknown;
		AddressMode address_mode_w = AddressMode::Unknown;
		FilterMode mag_filter = FilterMode::Unknown;
		FilterMode min_filter = FilterMode::Unknown;
		MipmapFilterMode mipmap_filter = MipmapFilterMode::Unknown;
		std::uint8_t max_anisotropy = 1u; // 1 means no anisotropic filtering
		CompareFunction compare_function = CompareFunction::Unknown;
		float min_lod = 0.0f;
		float max_lod = std::numeric_limits<float>::max();
	};

	class Sampler {
	public:
		using UniqueHandle = std::unique_ptr<
			struct SamplerImplementation,
			void(*)(struct SamplerImplementation*)
		>;

	private:
		template <class Native>
		friend struct CreatePipelineResourceGroup;
		template <class Native>
		friend struct execution::ExecuteCommands;

		UniqueHandle m_impl;

	public:
		Sampler() noexcept
			: m_impl(nullptr, nullptr) {
		}

		explicit Sampler(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}
	};

} // namespace fyuu_rhi
