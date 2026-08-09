module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
#endif

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

	struct Bitmap {
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		BitmapFormat format = BitmapFormat::R8G8B8A8;
		std::vector<std::byte> pixels;

		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] std::size_t BytesPerPixel() const noexcept;
		[[nodiscard]] bool Valid() const noexcept;
		void Serialize(std::filesystem::path const& path) const;
		[[nodiscard]] static Bitmap Deserialize(std::filesystem::path const& path);
	};

}
