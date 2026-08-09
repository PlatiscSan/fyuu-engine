module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
#endif

export module fyuu_asset:mesh;

#if defined(__cpp_lib_modules)
import std;
#endif

export namespace fyuu_asset {

	enum class MeshIndexFormat : std::uint8_t {
		UInt16,
		UInt32
	};

	struct Mesh {
		std::uint32_t vertex_stride = 0;
		std::vector<std::byte> vertices;
		MeshIndexFormat index_format = MeshIndexFormat::UInt32;
		std::vector<std::byte> indices;

		[[nodiscard]] std::size_t VertexCount() const noexcept;
		[[nodiscard]] std::size_t IndexCount() const noexcept;
		[[nodiscard]] bool Valid() const noexcept;
		void Serialize(std::filesystem::path const& path) const;
		[[nodiscard]] static Mesh Deserialize(std::filesystem::path const& path);
	};

}
