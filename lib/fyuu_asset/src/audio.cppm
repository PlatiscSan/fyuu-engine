module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#endif
#include <nlohmann/json.hpp>

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

	// Interleaved decoded PCM. Encoded files such as WAV, FLAC and Ogg belong
	// to the import pipeline and are converted into this runtime representation.
	struct Audio {
		std::uint32_t sample_rate = 0;
		std::uint16_t channels = 0;
		AudioFormat format = AudioFormat::Float32;
		std::vector<std::byte> samples;

		[[nodiscard]] std::size_t BytesPerSample() const noexcept {
			switch (format) {
			case AudioFormat::UInt8:
				return 1;
			case AudioFormat::Int16:
				return 2;
			case AudioFormat::Int24:
				return 3;
			case AudioFormat::Int32:
			case AudioFormat::Float32:
				return 4;
			case AudioFormat::Float64:
				return 8;
			}
			return 0;
		}

		[[nodiscard]] std::size_t FrameCount() const noexcept {
			auto frame_size = BytesPerSample() * channels;
			return frame_size == 0 ? 0 : samples.size() / frame_size;
		}

		[[nodiscard]] bool Valid() const noexcept {
			auto bytes_per_sample = BytesPerSample();
			if (sample_rate == 0 || channels == 0 || bytes_per_sample == 0) {
				return false;
			}
			auto frame_size = bytes_per_sample * channels;
			return !samples.empty() && samples.size() % frame_size == 0;
		}

		void Serialize(std::filesystem::path const& path) const {
			if (!Valid()) {
				throw std::runtime_error("Cannot serialize invalid audio");
			}

			auto binary = path;
			binary.replace_extension(".bin");
			auto temporary_binary = binary;
			temporary_binary += ".tmp";
			{
				std::ofstream output(temporary_binary, std::ios::binary | std::ios::trunc);
				if (!output) {
					throw std::runtime_error("Failed to open audio data file");
				}
				output.write(
					reinterpret_cast<char const*>(samples.data()),
					static_cast<std::streamsize>(samples.size())
				);
				if (!output) {
					throw std::runtime_error("Failed to write audio data file");
				}
			}

			auto temporary_config = path;
			temporary_config += ".tmp";
			{
				nlohmann::json document{
					{ "sample_rate", sample_rate },
					{ "channels", channels },
					{ "format", static_cast<std::uint8_t>(format) }
				};
				std::ofstream output(temporary_config, std::ios::binary | std::ios::trunc);
				if (!output) {
					throw std::runtime_error("Failed to open audio configuration file");
				}
				output << document.dump(2);
				if (!output) {
					throw std::runtime_error("Failed to write audio configuration file");
				}
			}

			std::error_code error;
			std::filesystem::rename(temporary_binary, binary, error);
			if (error) {
				std::filesystem::remove(binary, error);
				error.clear();
				std::filesystem::rename(temporary_binary, binary, error);
			}
			if (error) {
				throw std::runtime_error("Failed to publish audio data file");
			}

			std::filesystem::rename(temporary_config, path, error);
			if (error) {
				std::filesystem::remove(path, error);
				error.clear();
				std::filesystem::rename(temporary_config, path, error);
			}
			if (error) {
				throw std::runtime_error("Failed to publish audio configuration file");
			}
		}

		[[nodiscard]] static Audio Deserialize(std::filesystem::path const& path) {
			std::ifstream config(path, std::ios::binary);
			if (!config) {
				throw std::runtime_error("Failed to open audio configuration file");
			}
			auto document = nlohmann::json::parse(config);
			Audio audio{
				.sample_rate = document.at("sample_rate").get<std::uint32_t>(),
				.channels = document.at("channels").get<std::uint16_t>(),
				.format = static_cast<AudioFormat>(document.at("format").get<std::uint8_t>()),
				.samples = {}
			};

			auto binary = path;
			binary.replace_extension(".bin");
			std::error_code error;
			auto size = std::filesystem::file_size(binary, error);
			if (error || size > audio.samples.max_size()) {
				throw std::runtime_error("Failed to read audio data file size");
			}
			audio.samples.resize(static_cast<std::size_t>(size));

			std::ifstream input(binary, std::ios::binary);
			if (!input) {
				throw std::runtime_error("Failed to open audio data file");
			}
			input.read(
				reinterpret_cast<char*>(audio.samples.data()),
				static_cast<std::streamsize>(audio.samples.size())
			);
			if (!input || !audio.Valid()) {
				throw std::runtime_error("Audio data does not match its configuration");
			}
			return audio;
		}
	};

}
