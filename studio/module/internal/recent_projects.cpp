module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <cstdint>
#include <filesystem>
#include <span>
#endif

module fyuu_studio:recent_projects;

import fyuu_desktop;
#if defined(__cpp_lib_modules)
import std;
#endif

namespace fyuu_studio {
	class RecentProjects final {
	private:
		static constexpr std::uint32_t MaximumCount = 12u;
		static constexpr std::uint32_t MaximumPathSize = 1024u * 1024u;
		std::filesystem::path m_storage_path;
		std::vector<std::filesystem::path> m_projects;

		static void WriteUInt32(std::ostream& output, std::uint32_t value) {
			for (std::uint32_t offset = 0u; offset < 32u; offset += 8u)
				output.put(static_cast<char>((value >> offset) & 0xffu));
		}

		static std::uint32_t ReadUInt32(std::istream& input) {
			std::uint32_t value = 0u;
			for (std::uint32_t offset = 0u; offset < 32u; offset += 8u) {
				auto const byte = input.get();
				if (byte == std::char_traits<char>::eof())
					return MaximumPathSize + 1u;
				value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << offset;
			}
			return value;
		}

		void Save() const {
			auto temporary = m_storage_path;
			temporary += ".tmp";
			std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
			WriteUInt32(output, static_cast<std::uint32_t>(m_projects.size()));
			for (auto const& project : m_projects) {
				auto const text = project.generic_u8string();
				WriteUInt32(output, static_cast<std::uint32_t>(text.size()));
				output.write(
					reinterpret_cast<char const*>(text.data()),
					static_cast<std::streamsize>(text.size())
				);
			}
			output.close();
			if (!output)
				return;
			std::error_code error;
			std::filesystem::rename(temporary, m_storage_path, error);
			if (error) {
				std::filesystem::remove(m_storage_path, error);
				error.clear();
				std::filesystem::rename(temporary, m_storage_path, error);
			}
		}

	public:
		RecentProjects() :
			m_storage_path(fyuu_desktop::PreferencePath("Fyuu", "Studio") / "recent-projects") {
			std::ifstream input{m_storage_path, std::ios::binary};
			if (!input)
				return;
			auto const count = ReadUInt32(input);
			for (std::uint32_t index = 0u; index < std::min(count, MaximumCount); ++index) {
				auto const size = ReadUInt32(input);
				if (size > MaximumPathSize)
					break;
				std::u8string text(size, u8'\0');
				input.read(reinterpret_cast<char*>(text.data()), static_cast<std::streamsize>(size));
				if (!input)
					break;
				std::filesystem::path path{text};
				std::error_code error;
				if (std::filesystem::is_regular_file(path, error))
					m_projects.emplace_back(std::filesystem::weakly_canonical(path, error));
			}
		}

		[[nodiscard]] std::span<std::filesystem::path const> Projects() const noexcept {
			return m_projects;
		}

		void Touch(std::filesystem::path const& path) {
			std::error_code error;
			auto canonical = std::filesystem::weakly_canonical(path, error);
			if (error)
				canonical = path;
			std::erase(m_projects, canonical);
			m_projects.insert(m_projects.begin(), std::move(canonical));
			if (m_projects.size() > MaximumCount)
				m_projects.resize(MaximumCount);
			Save();
		}
	};
}
