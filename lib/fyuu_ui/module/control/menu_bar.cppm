module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <vector>
#include <string>
#include <cstdint>
#include <span>
#endif
export module fyuu_ui:control_menu_bar;
#if defined(__cpp_lib_modules)
import std;
#endif
export namespace fyuu_ui {

	/// One menu command or submenu. Children turn the entry into a submenu header;
	/// checked is presentation state and does not imply automatic toggling.
	struct MenuEntry {
		std::string title;
		bool enabled = true;
		bool checked = false;
		std::vector<MenuEntry> children;
	};

	/// Stores both the immutable menu model and index paths for transient popup
	/// state. Paths contain one zero-based child index per nesting level.
	struct MenuBar {
		std::string title;
		std::vector<MenuEntry> entries;
		std::vector<std::uint32_t> open_path;
		std::vector<std::uint32_t> hover_path;
		std::vector<std::uint32_t> pressed_path;
	};

	/// Resolves an index path without allocating; returns null for an empty or
	/// stale path so callers can safely retain paths across model replacement.
	[[nodiscard]] MenuEntry const* FindMenuEntry(
	    MenuBar const& bar,
	    std::span<std::uint32_t const> path
	) noexcept {
		if (path.empty())
			return nullptr;
		auto const* current = bar.entries.data();
		auto count = bar.entries.size();
		for (std::size_t depth = 0u; depth < path.size(); ++depth) {
			auto const index = path[depth];
			if (current == nullptr || index >= count)
				return nullptr;
			current += index;
			if (depth + 1u == path.size())
				return current;
			count = current->children.size();
			current = current->children.data();
		}
		return nullptr;
	}

} // namespace fyuu_ui
