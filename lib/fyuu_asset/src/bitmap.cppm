module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>
#endif
#include <nlohmann/json.hpp>

export module fyuu_asset:bitmap;

#if defined(__cpp_lib_modules)
import std;
#endif

export namespace fyuu_asset {

	enum class BitmapFormat : std::uint8_t {
		R8,
		R8G8,
		R8G8B8,
		R8G8B8A8,
		R16,
		R16G16,
		R16G16B16,
		R16G16B16A16,
		R32Float,
		R32G32Float,
		R32G32B32Float,
		R32G32B32A32Float
	};

	// CPU-side tightly packed bitmap. Compressed source files belong to the
	// import pipeline; pixels here are directly addressable decoded data.
	struct Bitmap {
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		BitmapFormat format = BitmapFormat::R8G8B8A8;
		std::vector<std::byte> pixels;

		[[nodiscard]] bool Empty() const noexcept {
			return width == 0 || height == 0;
		}

		[[nodiscard]] std::size_t BytesPerPixel() const noexcept {
			switch (format) {
			case BitmapFormat::R8:
				return 1;
			case BitmapFormat::R8G8:
			case BitmapFormat::R16:
				return 2;
			case BitmapFormat::R8G8B8:
				return 3;
			case BitmapFormat::R8G8B8A8:
			case BitmapFormat::R16G16:
			case BitmapFormat::R32Float:
				return 4;
			case BitmapFormat::R32G32Float:
				return 8;
			case BitmapFormat::R16G16B16:
				return 6;
			case BitmapFormat::R16G16B16A16:
				return 8;
			case BitmapFormat::R32G32B32Float:
				return 12;
			case BitmapFormat::R32G32B32A32Float:
				return 16;
			}
			return 0;
		}

		[[nodiscard]] bool Valid() const noexcept {
			if (Empty()) {
				return width == 0 && height == 0 && pixels.empty();
			}

			auto bytes_per_pixel = BytesPerPixel();
			auto pixel_count = static_cast<std::size_t>(width) * height;
			return bytes_per_pixel != 0 &&
				pixel_count <= pixels.max_size() / bytes_per_pixel &&
				pixels.size() == pixel_count * bytes_per_pixel;
		}

		void Serialize(std::filesystem::path const& path) const {
			if (!Valid()) {
				throw std::runtime_error("Cannot serialize an invalid bitmap");
			}

			auto binary = path;
			binary.replace_extension(".bin");
			auto temporary_binary = binary;
			temporary_binary += ".tmp";
			{
				std::ofstream output(temporary_binary, std::ios::binary | std::ios::trunc);
				if (!output) {
					throw std::runtime_error("Failed to open bitmap data file");
				}
				output.write(
					reinterpret_cast<char const*>(pixels.data()),
					static_cast<std::streamsize>(pixels.size())
				);
				if (!output) {
					throw std::runtime_error("Failed to write bitmap data file");
				}
			}

			auto temporary_config = path;
			temporary_config += ".tmp";
			{
				nlohmann::json document{
					{ "width", width },
					{ "height", height },
					{ "format", static_cast<std::uint8_t>(format) }
				};
				std::ofstream output(temporary_config, std::ios::binary | std::ios::trunc);
				if (!output) {
					throw std::runtime_error("Failed to open bitmap configuration file");
				}
				output << document.dump(2);
				if (!output) {
					throw std::runtime_error("Failed to write bitmap configuration file");
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
				throw std::runtime_error("Failed to publish bitmap data file");
			}

			std::filesystem::rename(temporary_config, path, error);
			if (error) {
				std::filesystem::remove(path, error);
				error.clear();
				std::filesystem::rename(temporary_config, path, error);
			}
			if (error) {
				throw std::runtime_error("Failed to publish bitmap configuration file");
			}
		}

		[[nodiscard]] static Bitmap Deserialize(std::filesystem::path const& path) {
			std::ifstream config(path, std::ios::binary);
			if (!config) {
				throw std::runtime_error("Failed to open bitmap configuration file");
			}
			auto document = nlohmann::json::parse(config);

			Bitmap bitmap{
				.width = document.at("width").get<std::uint32_t>(),
				.height = document.at("height").get<std::uint32_t>(),
				.format = static_cast<BitmapFormat>(document.at("format").get<std::uint8_t>()),
				.pixels = {}
			};
			auto binary = path;
			binary.replace_extension(".bin");
			std::error_code error;
			auto size = std::filesystem::file_size(binary, error);
			if (error || size > bitmap.pixels.max_size()) {
				throw std::runtime_error("Failed to read bitmap data file size");
			}
			bitmap.pixels.resize(static_cast<std::size_t>(size));

			std::ifstream input(binary, std::ios::binary);
			if (!input) {
				throw std::runtime_error("Failed to open bitmap data file");
			}
			input.read(
				reinterpret_cast<char*>(bitmap.pixels.data()),
				static_cast<std::streamsize>(bitmap.pixels.size())
			);
			if (!input || !bitmap.Valid()) {
				throw std::runtime_error("Bitmap data does not match its configuration");
			}
			return bitmap;
		}
	};

}
