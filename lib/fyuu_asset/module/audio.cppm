module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
#endif

export module fyuu_asset:audio;

#if defined(__cpp_lib_modules)
import std;
#endif

export namespace fyuu_asset {

	enum class AudioFormat : std::uint8_t {
		UInt8,
		Int16,
		Int24,
		Int32,
		Float32,
		Float64
	};

	struct Audio {
		std::uint32_t sample_rate = 0;
		std::uint16_t channels = 0;
		AudioFormat format = AudioFormat::Float32;
		std::vector<std::byte> samples;

		[[nodiscard]] std::size_t BytesPerSample() const noexcept;
		[[nodiscard]] std::size_t FrameCount() const noexcept;
		[[nodiscard]] bool Valid() const noexcept;
		void Serialize(std::filesystem::path const& path) const;
		[[nodiscard]] static Audio Deserialize(std::filesystem::path const& path);
	};

}
