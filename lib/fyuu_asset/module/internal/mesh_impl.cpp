module;
#include <fstream>
#include <nlohmann/json.hpp>

module fyuu_asset:mesh_impl;

#if defined(__cpp_lib_modules)
import std;
#endif
import :mesh;

namespace fyuu_asset {

	std::size_t Mesh::VertexCount() const noexcept {
		return vertex_stride == 0 ? 0 : vertices.size() / vertex_stride;
	}

	std::size_t Mesh::IndexCount() const noexcept {
		auto stride = index_format == MeshIndexFormat::UInt16 ? 2u : 4u;
		return indices.size() / stride;
	}

	bool Mesh::Valid() const noexcept {
		if (vertex_stride == 0 || vertices.empty() || vertices.size() % vertex_stride != 0) {
			return false;
		}
		auto index_stride = index_format == MeshIndexFormat::UInt16 ? 2u : 4u;
		return !indices.empty() && indices.size() % index_stride == 0;
	}

	void Mesh::Serialize(std::filesystem::path const& path) const {
		if (!Valid()) {
			throw std::runtime_error("Cannot serialize invalid mesh");
		}
		auto binary = path;
		binary.replace_extension(".bin");
		auto temporary_binary = binary;
		temporary_binary += ".tmp";
		{
			std::ofstream output(temporary_binary, std::ios::binary | std::ios::trunc);
			output.write(
				reinterpret_cast<char const*>(vertices.data()),
				static_cast<std::streamsize>(vertices.size())
			);
			output.write(
				reinterpret_cast<char const*>(indices.data()),
				static_cast<std::streamsize>(indices.size())
			);
			if (!output) {
				throw std::runtime_error("Failed to write mesh data file");
			}
		}
		auto temporary_config = path;
		temporary_config += ".tmp";
		{
			nlohmann::json document{
				{ "vertex_stride", vertex_stride },
				{ "vertex_size", vertices.size() },
				{ "index_format", static_cast<std::uint8_t>(index_format) },
				{ "index_size", indices.size() }
			};
			std::ofstream output(temporary_config, std::ios::binary | std::ios::trunc);
			output << document.dump(2);
			if (!output) {
				throw std::runtime_error("Failed to write mesh configuration file");
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
			throw std::runtime_error("Failed to publish mesh data file");
		}
		std::filesystem::rename(temporary_config, path, error);
		if (error) {
			std::filesystem::remove(path, error);
			error.clear();
			std::filesystem::rename(temporary_config, path, error);
		}
		if (error) {
			throw std::runtime_error("Failed to publish mesh configuration file");
		}
	}

	Mesh Mesh::Deserialize(std::filesystem::path const& path) {
		std::ifstream config(path, std::ios::binary);
		if (!config) {
			throw std::runtime_error("Failed to open mesh configuration file");
		}
		auto document = nlohmann::json::parse(config);
		auto vertex_size = document.at("vertex_size").get<std::size_t>();
		auto index_size = document.at("index_size").get<std::size_t>();
		Mesh mesh{
			.vertex_stride = document.at("vertex_stride").get<std::uint32_t>(),
			.vertices = std::vector<std::byte>(vertex_size),
			.index_format = static_cast<MeshIndexFormat>(document.at("index_format").get<std::uint8_t>()),
			.indices = std::vector<std::byte>(index_size)
		};
		auto binary = path;
		binary.replace_extension(".bin");
		std::ifstream input(binary, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Failed to open mesh data file");
		}
		input.read(
			reinterpret_cast<char*>(mesh.vertices.data()),
			static_cast<std::streamsize>(mesh.vertices.size())
		);
		input.read(
			reinterpret_cast<char*>(mesh.indices.data()),
			static_cast<std::streamsize>(mesh.indices.size())
		);
		if (!input || input.peek() != std::char_traits<char>::eof() || !mesh.Valid()) {
			throw std::runtime_error("Mesh data does not match its configuration");
		}
		return mesh;
	}

}
