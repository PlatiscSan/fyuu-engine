module;
#include <version>
#if !defined(__cpp_lib_modules)
// C++98: project text, binary streams, and validation errors.
#include <string>
#include <fstream>
#include <stdexcept>
// C++11: fixed-width binary fields.
#include <cstdint>
// C++17: borrowed names and project paths.
#include <string_view>
#include <filesystem>
#endif // !defined(__cpp_lib_modules)

module fyuu_studio:project;

#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_studio {
	class Project final {
	private:
		static constexpr std::string_view Magic = "FYUUPROJ";
		static constexpr std::uint32_t FormatVersion = 1u;
		static constexpr std::uint32_t MaximumStringSize = 1024u * 1024u;
		std::string m_name;
		std::filesystem::path m_asset_path = "asset";

		static void WriteUInt32(std::ostream& output, std::uint32_t value) {
			for (std::uint32_t offset = 0u; offset < 32u; offset += 8u) {
				output.put(static_cast<char>((value >> offset) & 0xffu));
			}
		}

		static std::uint32_t ReadUInt32(std::istream& input) {
			std::uint32_t value = 0u;
			for (std::uint32_t offset = 0u; offset < 32u; offset += 8u) {
				auto const byte = input.get();
				if (byte == std::char_traits<char>::eof()) {
					throw std::runtime_error("Unexpected end of project file");
				}
				value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << offset;
			}
			return value;
		}

		static void WriteString(std::ostream& output, std::string_view value) {
			if (value.size() > MaximumStringSize) {
				throw std::runtime_error("Project string is too large");
			}
			WriteUInt32(output, static_cast<std::uint32_t>(value.size()));
			output.write(value.data(), static_cast<std::streamsize>(value.size()));
		}

		static std::string ReadString(std::istream& input) {
			auto const size = ReadUInt32(input);
			if (size > MaximumStringSize) {
				throw std::runtime_error("Project string is too large");
			}
			std::string value(size, '\0');
			input.read(value.data(), static_cast<std::streamsize>(size));
			if (!input) {
				throw std::runtime_error("Unexpected end of project file");
			}
			return value;
		}

	public:
		explicit Project(std::string_view name, std::filesystem::path const& asset_path = "asset")
			: m_name(name), m_asset_path(asset_path) {
			if (m_name.empty()) {
				throw std::invalid_argument("Project name cannot be empty");
			}
			if (m_asset_path.empty() || m_asset_path.is_absolute()) {
				throw std::invalid_argument("Project asset path must be relative");
			}
		}

		static Project Deserialize(std::filesystem::path const& path) {
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				throw std::runtime_error("Unable to open project file");
			}
			std::string magic(Magic.size(), '\0');
			input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
			if (!input || magic != Magic) {
				throw std::runtime_error("Invalid project file signature");
			}
			if (ReadUInt32(input) != FormatVersion) {
				throw std::runtime_error("Unsupported project format version");
			}
			Project project{ ReadString(input) };
			project.m_asset_path = ReadString(input);
			if (project.m_asset_path.empty() || project.m_asset_path.is_absolute()) {
				throw std::runtime_error("Project asset path must be relative");
			}
			return project;
		}

		void Serialize(std::filesystem::path const& path) const {
			auto temporary = path;
			temporary += ".tmp";
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				output.write(Magic.data(), static_cast<std::streamsize>(Magic.size()));
				WriteUInt32(output, FormatVersion);
				WriteString(output, m_name);
				WriteString(output, m_asset_path.generic_string());
				if (!output) {
					throw std::runtime_error("Unable to write project file");
				}
			}
			std::error_code error;
			std::filesystem::rename(temporary, path, error);
			if (error) {
				std::filesystem::remove(path, error);
				error.clear();
				std::filesystem::rename(temporary, path, error);
			}
			if (error) {
				throw std::runtime_error("Unable to publish project file");
			}
		}

		std::filesystem::path ResolveAssetPath(
			std::filesystem::path const& project_path
		) const {
			return project_path.parent_path() / m_asset_path;
		}
	};
}
