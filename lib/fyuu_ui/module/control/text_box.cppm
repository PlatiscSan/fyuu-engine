module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <utility>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#endif
export module fyuu_ui:control_text_box;
#if defined(__cpp_lib_modules)
import std;
#endif
export namespace fyuu_ui {
	/// Single-line UTF-8 editor state. Caret and selection offsets are byte offsets
	/// kept on code-point boundaries; horizontal_offset is measured in logical pixels.
	struct TextBox {
		std::string text;
		std::string placeholder;
		bool read_only = false;
		bool focused = false;
		std::size_t caret_offset = 0u;
		std::size_t selection_anchor = 0u;
		float horizontal_offset = 0.0f;
		std::string edit_snapshot;
	};

	/// Returns the previous UTF-8 code-point boundary, clamping an oversized offset.
	[[nodiscard]] inline std::size_t PreviousTextOffset(
	    std::string_view text,
	    std::size_t offset
	) noexcept {
		offset = std::min(offset, text.size());
		if (offset == 0u)
			return 0u;
		--offset;
		while (offset != 0u && (static_cast<unsigned char>(text[offset]) & 0xC0u) == 0x80u)
			--offset;
		return offset;
	}

	/// Returns the next UTF-8 code-point boundary, clamping an oversized offset.
	[[nodiscard]] inline std::size_t NextTextOffset(
	    std::string_view text,
	    std::size_t offset
	) noexcept {
		offset = std::min(offset, text.size());
		if (offset == text.size())
			return offset;
		++offset;
		while (offset < text.size() &&
		    (static_cast<unsigned char>(text[offset]) & 0xC0u) == 0x80u) {
			++offset;
		}
		return offset;
	}

	/// Selection is represented by an anchor and the moving caret; equal ends mean empty.
	[[nodiscard]] inline bool HasTextSelection(TextBox const& value) noexcept {
		return value.caret_offset != value.selection_anchor;
	}

	[[nodiscard]] inline std::pair<std::size_t, std::size_t> TextSelection(
	    TextBox const& value
	) noexcept {
		return std::minmax(value.caret_offset, value.selection_anchor);
	}

	inline void SelectAllText(TextBox& value) noexcept {
		value.selection_anchor = 0u;
		value.caret_offset = value.text.size();
	}

	inline void CollapseTextSelection(TextBox& value, std::size_t offset) noexcept {
		value.caret_offset = std::min(offset, value.text.size());
		value.selection_anchor = value.caret_offset;
	}

	/// Erases the normalized selection and collapses both ends at its beginning.
	inline void EraseTextSelection(TextBox& value) {
		if (!HasTextSelection(value))
			return;
		auto const [first, last] = TextSelection(value);
		value.text.erase(first, last - first);
		CollapseTextSelection(value, first);
	}

	/// Replaces the active selection, or inserts at the caret when it is empty.
	inline void InsertText(TextBox& value, std::string_view text) {
		EraseTextSelection(value);
		value.text.insert(value.caret_offset, text);
		CollapseTextSelection(value, value.caret_offset + text.size());
	}

	inline void DeleteTextBackward(TextBox& value) {
		if (HasTextSelection(value)) {
			EraseTextSelection(value);
			return;
		}
		auto const previous = PreviousTextOffset(value.text, value.caret_offset);
		value.text.erase(previous, value.caret_offset - previous);
		CollapseTextSelection(value, previous);
	}

	inline void DeleteTextForward(TextBox& value) {
		if (HasTextSelection(value)) {
			EraseTextSelection(value);
			return;
		}
		auto const next = NextTextOffset(value.text, value.caret_offset);
		value.text.erase(value.caret_offset, next - value.caret_offset);
	}

	inline void MoveTextCaret(TextBox& value, std::size_t offset, bool extend_selection) noexcept {
		if (!extend_selection)
			value.selection_anchor = std::min(offset, value.text.size());
		value.caret_offset = std::min(offset, value.text.size());
	}

	/// Selects the ASCII word containing offset. Non-ASCII UTF-8 bytes are treated
	/// as word content so a multi-byte name is not split by byte classification.
	inline void SelectTextWord(TextBox& value, std::size_t offset) noexcept {
		offset = std::min(offset, value.text.size());
		auto first = offset;
		auto last = offset;
		auto IsWord = [](unsigned char character) {
			return character >= 0x80u || std::isalnum(character) != 0 || character == '_';
		};
		while (first != 0u && IsWord(static_cast<unsigned char>(value.text[first - 1u])))
			--first;
		while (last < value.text.size() && IsWord(static_cast<unsigned char>(value.text[last])))
			++last;
		value.selection_anchor = first;
		value.caret_offset = last;
	}

	[[nodiscard]] inline std::string CopySelectedText(TextBox const& value) {
		auto const [first, last] = TextSelection(value);
		return value.text.substr(first, last - first);
	}

	[[nodiscard]] inline std::string CutSelectedText(TextBox& value) {
		auto result = CopySelectedText(value);
		EraseTextSelection(value);
		return result;
	}

	/// Starts a commit/cancel transaction by recording the current text inline.
	inline void BeginTextEdit(TextBox& value) {
		value.edit_snapshot = value.text;
	}

	/// Accepts the current value and reports whether it differs from the snapshot.
	[[nodiscard]] inline bool CommitTextEdit(TextBox& value) {
		auto const changed = value.text != value.edit_snapshot;
		value.edit_snapshot = value.text;
		return changed;
	}

	/// Restores the transaction snapshot and reports whether text was reverted.
	[[nodiscard]] inline bool CancelTextEdit(TextBox& value) {
		auto const changed = value.text != value.edit_snapshot;
		value.text = value.edit_snapshot;
		CollapseTextSelection(value, value.text.size());
		return changed;
	}

	/// Maps a logical x coordinate to the nearest caret boundary using the host's
	/// exact renderer-backed prefix measurement.
	template <class Measure>
	[[nodiscard]] inline std::size_t TextOffsetAt(
	    TextBox const& value,
	    float position,
	    Measure const& measure
	) {
		std::size_t offset = 0u;
		for (auto next = NextTextOffset(value.text, offset); offset < value.text.size();
		     next = NextTextOffset(value.text, offset)) {
			auto const left = measure(std::string_view{value.text}.substr(0u, offset));
			auto const right = measure(std::string_view{value.text}.substr(0u, next));
			if (position < (left + right) * 0.5f)
				return offset;
			offset = next;
		}
		return value.text.size();
	}

	/// Adjusts horizontal_offset just enough to keep the current caret in view.
	template <class Measure>
	inline void EnsureTextCaretVisible(
	    TextBox& value,
	    float viewport_width,
	    Measure const& measure
	) {
		auto const caret = measure(
		    std::string_view{value.text}.substr(0u, std::min(value.caret_offset, value.text.size()))
		);
		if (caret < value.horizontal_offset)
			value.horizontal_offset = caret;
		else if (caret > value.horizontal_offset + viewport_width)
			value.horizontal_offset = caret - std::max(0.0f, viewport_width);
		value.horizontal_offset = std::max(0.0f, value.horizontal_offset);
	}
} // namespace fyuu_ui
