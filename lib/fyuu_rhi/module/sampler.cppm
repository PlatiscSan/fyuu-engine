module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <limits>

#include <cstdint>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:sampler;
import :fast_pimpl;
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

	/// Immutable filtering, addressing, comparison, anisotropy, and LOD state.
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

	/** @brief Move-only owner of immutable texture-sampling state. */
	class Sampler {
	public:
		using ImplementationStorage = details::FastPimpl<struct SamplerImplementation, 64u>;

	private:
		template <class Native>
		friend struct CreatePipelineResourceGroup;
		template <class Native>
		friend struct execution::ExecuteCommands;

		ImplementationStorage m_impl;

	public:
		Sampler() noexcept;

		explicit Sampler(ImplementationStorage&& impl) noexcept;

		Sampler(Sampler const&) = delete;
		Sampler& operator=(Sampler const&) = delete;
		Sampler(Sampler&& other) noexcept;
		Sampler& operator=(Sampler&& other) noexcept;
		~Sampler() noexcept;

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}
	};

} // namespace fyuu_rhi
