module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <vector>
#include <algorithm>
#include <functional>
#include <cctype>
#include <string>
#include <cstdint>
#include <system_error>
#include <filesystem>
#include <ranges>
#endif

module fyuu_ui:file_dialog_impl;
#if defined(__cpp_lib_modules)
import std;
#endif
import :file_dialog;

namespace fyuu_ui {
	namespace {
		// filesystem::path::string uses the active Windows ANSI code page and may
		// throw for perfectly valid Unicode names. FyuuUI text is UTF-8 throughout.
		std::string PathText(std::filesystem::path const& path) {
			auto const source = path.generic_u8string();
			return source |
			    std::views::transform([](char8_t code_unit) {
				    return static_cast<char>(code_unit);
			    }) |
			    std::ranges::to<std::string>();
		}

		std::string Lowercase(std::string value) {
			std::ranges::transform(
			    value,
			    value.begin(),
			    [](unsigned char character) {
				    return static_cast<char>(std::tolower(character));
			    }
			);
			return value;
		}
	} // namespace

	FileDialogue::FileDialogue(DialogHost& host) noexcept
	    : m_host(&host), m_tree(&host.Tree()), m_events(&host.Events()), m_message_box(host) {}

	FileDialogue::~FileDialogue() noexcept {
		if (m_open)
			m_host->Close(m_session.window_id);
	}

	void FileDialogue::ShowOpen(
	    FileDialogOptions const& options,
	    std::function<void(std::filesystem::path const&)> callback
	) {
		Show(FileDialogMode::Open, options, std::move(callback));
	}

	void FileDialogue::ShowSave(
	    FileDialogOptions const& options,
	    std::function<void(std::filesystem::path const&)> callback
	) {
		Show(FileDialogMode::Save, options, std::move(callback));
	}

	void FileDialogue::Show(
	    FileDialogMode mode,
	    FileDialogOptions const& options,
	    std::function<void(std::filesystem::path const&)> callback
	) {
		// A helper owns at most one dialog. Replacing it completes the previous
		// request as a cancellation before constructing the next logical subtree.
		if (m_open)
			Complete({});
		else
			m_message_box.Close();
		m_session = {};
		m_session.mode = mode;
		m_session.options = options;
		m_session.callback = std::move(callback);
		std::error_code initial_directory_error;
		m_session.directory = options.initial_directory.empty() ?
		    std::filesystem::current_path(initial_directory_error) :
		    options.initial_directory;
		if (initial_directory_error ||
		    !std::filesystem::is_directory(m_session.directory, initial_directory_error)) {
			initial_directory_error.clear();
			m_session.directory = std::filesystem::current_path(initial_directory_error);
		}
		if (!initial_directory_error) {
			m_session.directory =
			    std::filesystem::weakly_canonical(m_session.directory, initial_directory_error);
		}
		if (initial_directory_error)
			m_session.directory.clear();

		auto window = m_host->Open(
		    Window{
		        options.title.empty() ? (mode == FileDialogMode::Open ? "Open file" : "Save file") :
		                                options.title,
		        options.position,
		        options.size,
		        true,
		        InteractionState::Normal,
		        {420.0f, 320.0f},
		        true
		    }
		);
		m_session.window_id = window.GetID();
		m_open = true;
		RebuildContent();
		m_host->Activate(m_session.window_id);
	}

	void FileDialogue::RebuildContent() {
		auto& session = m_session;
		session.subscriptions.clear();
		if (session.content_id != 0u)
			m_events->Remove(*m_tree, session.content_id);
		session.entries.clear();
		session.button_ids.clear();
		session.visible_entries.clear();
		session.selected_path.clear();

		std::error_code error;
		if (session.directory.empty()) {
#if defined(_WIN32)
			// std::filesystem has no volume enumeration API. Generate the finite drive
			// namespace, then retain accessible directory roots.
			session.entries = std::views::iota('A', static_cast<char>('Z' + 1)) |
			    std::views::transform([](char letter) {
				    return std::filesystem::path{std::string{letter} + ":/"};
			    }) |
			    std::views::filter([](std::filesystem::path const& root) {
				    std::error_code root_error;
				    return std::filesystem::is_directory(root, root_error);
			    }) |
			    std::ranges::to<std::vector<std::filesystem::path>>();
#else
			// POSIX exposes one namespace; mounted volumes are ordinary descendants.
			session.entries.emplace_back("/");
#endif
		} else {
			for (
			    std::filesystem::directory_iterator iterator{session.directory, error}, end;
			    !error && iterator != end;
			    iterator.increment(error)
			) {
				auto const& entry = *iterator;
				if (entry.is_directory(error) || entry.is_regular_file(error)) {
					session.entries.emplace_back(entry.path());
				}
				error.clear();
			}
		}
		std::ranges::sort(session.entries, [](auto const& left, auto const& right) {
			std::error_code left_error;
			std::error_code right_error;
			auto const left_directory = std::filesystem::is_directory(left, left_error);
			auto const right_directory = std::filesystem::is_directory(right, right_error);
			if (left_directory != right_directory)
				return left_directory;
			return Lowercase(PathText(left.filename())) < Lowercase(PathText(right.filename()));
		});

		auto window = m_tree->GetNode(session.window_id);
		// Overlay lets the browser stretch while the footer remains attached to the
		// lower client edge. A single stack keeps its measured height during resize.
		auto content = window.AddChild(Overlay{true});
		session.content_id = content.GetID();
		LayoutProperties content_layout;
		content_layout.margin = {12.0f, 10.0f, 12.0f, 12.0f};
		content.SetLayout(content_layout);
		// BrowserRegion owns a second clip. Resizing can make the fixed-height row
		// stack taller than its assigned area; clipping here prevents rows from
		// painting through the bottom-anchored filename and action controls.
		auto browser_region = content.AddChild(Overlay{true});
		LayoutProperties browser_layout;
		browser_layout.margin.bottom =
		    session.mode == FileDialogMode::Open ? 40.0f : 78.0f;
		browser_region.SetLayout(browser_layout);
		// The path occupies the complete header row. Parent navigation belongs to
		// the directory listing below, alongside every other directory entry.
		auto navigation = browser_region.AddChild(
		    TextBox{
		        session.directory.empty() ?
#if defined(_WIN32)
		            "This PC" :
#else
		            "/" :
#endif
		            PathText(session.directory),
		        "Current directory",
		        false,
		        false,
		        0u
		    }
		);
		session.path_id = navigation.GetID();
		navigation.AsWidget<TextBox>().caret_offset = navigation.AsWidget<TextBox>().text.size();
		LayoutProperties navigation_layout;
		navigation_layout.height = 28.0f;
		navigation_layout.vertical_alignment = Alignment::Start;
		navigation.SetLayout(navigation_layout);

		// Only the middle list is clipped. Navigation and pagination remain visible
		// at the browser region's fixed top and bottom edges during window resize.
		auto entries_region = browser_region.AddChild(ScrollView{});
		LayoutProperties entries_layout;
		entries_layout.margin = {0.0f, 32.0f, 0.0f, 0.0f};
		entries_region.SetLayout(entries_layout);
		auto entry_list = entries_region.AddChild(StackPanel{Orientation::Vertical, 4.0f});
		auto parent_directory = session.directory.parent_path();
		auto has_parent = !session.directory.empty() && parent_directory != session.directory;
#if defined(_WIN32)
		// A drive root's browser parent is the virtual volume list. POSIX '/' is
		// already the real top and therefore never receives an artificial '..'.
		if (!session.directory.empty() && parent_directory == session.directory) {
			parent_directory.clear();
			has_parent = true;
		}
#endif
		if (has_parent) {
			auto parent =
			    entry_list.AddChild(FileItem{"..", true, false, true, InteractionState::Normal});
			session.visible_entries.emplace_back(parent.GetID(), parent_directory);
			session.subscriptions.emplace_back(m_events->Subscribe<ClickEvent>(parent, [this, parent_directory](ClickEvent& event) {
				m_session.pending = PendingAction::Navigate;
				m_session.pending_path = parent_directory;
				event.handled = true;
			}));
		}
		for (auto const& path : session.entries) {
			std::error_code type_error;
			auto const directory = std::filesystem::is_directory(path, type_error);
			auto extension = Lowercase(PathText(path.extension()));
			if (!extension.empty() && extension.front() == '.')
				extension.erase(extension.begin());
			auto const selectable = directory || session.options.filters.empty() ||
			    std::ranges::any_of(session.options.filters, [&extension](auto const& filter) {
				    return filter.extensions.empty() ||
				        std::ranges::any_of(filter.extensions, [&extension](auto candidate) {
					        if (!candidate.empty() && candidate.front() == '.')
						        candidate.erase(candidate.begin());
					        return Lowercase(std::move(candidate)) == extension;
				        });
			    });
			auto display_name = PathText(path.filename());
			if (display_name.empty())
				display_name = PathText(path.root_name());
			auto item = entry_list.AddChild(
			    FileItem{
			        display_name,
			        directory,
			        !directory && display_name == session.options.initial_file_name,
			        selectable,
			        InteractionState::Normal
			    }
			);
			session.visible_entries.emplace_back(item.GetID(), path);
			if (selectable) {
				session.subscriptions.emplace_back(m_events->Subscribe<ClickEvent>(item, [this, path, directory](ClickEvent& event) {
					m_session.pending =
					    directory ? PendingAction::Navigate : PendingAction::Select;
					m_session.pending_path = path;
					event.handled = true;
				}));
			}
		}
		auto footer = content.AddChild(StackPanel{Orientation::Vertical, 6.0f});
		LayoutProperties footer_layout;
		footer_layout.height = session.mode == FileDialogMode::Open ? 30.0f : 68.0f;
		footer_layout.vertical_alignment = Alignment::End;
		footer.SetLayout(footer_layout);
		// Opening selects an existing row, so a second editable representation of
		// that selection only adds ambiguity. Saving still needs a new file name.
		if (session.mode == FileDialogMode::Save) {
			auto file_name_row = footer.AddChild(Overlay{});
			LayoutProperties file_name_row_layout;
			file_name_row_layout.height = 30.0f;
			file_name_row.SetLayout(file_name_row_layout);
			auto label = file_name_row.AddChild(
			    TextBlock{"File name", {0.875f, 0.890f, 0.920f, 1.0f}, 13.0f}
			);
			LayoutProperties label_layout;
			label_layout.width = 72.0f;
			label_layout.vertical_alignment = Alignment::Center;
			label.SetLayout(label_layout);
			auto file_name = file_name_row.AddChild(
			    TextBox{
			        session.options.initial_file_name,
			        "Save as",
			        false,
			        false,
			        session.options.initial_file_name.size()
			    }
			);
			LayoutProperties file_name_layout;
			file_name_layout.margin.left = 78.0f;
			file_name.SetLayout(file_name_layout);
			session.file_name_id = file_name.GetID();
		}
		auto actions = footer.AddChild(StackPanel{Orientation::Horizontal, 8.0f});
		LayoutProperties actions_layout;
		actions_layout.horizontal_alignment = Alignment::End;
		actions.SetLayout(actions_layout);
		auto accept = actions.AddChild(
		    Button{session.mode == FileDialogMode::Open ? "Open" : "Save", true, true}
		);
		session.button_ids.emplace_back(accept.GetID());
		session.subscriptions.emplace_back(m_events->Subscribe<ClickEvent>(accept, [this](ClickEvent& event) {
			m_session.pending = PendingAction::Accept;
			event.handled = true;
		}));
		auto cancel = actions.AddChild(Button{"Cancel", true, false});
		session.button_ids.emplace_back(cancel.GetID());
		session.subscriptions.emplace_back(m_events->Subscribe<ClickEvent>(cancel, [this](ClickEvent& event) {
			m_session.pending = PendingAction::Cancel;
			event.handled = true;
		}));
	}

	void FileDialogue::Update() {
		m_message_box.Update();
		if (!m_open)
			return;
		auto const pending = m_session.pending;
		m_session.pending = PendingAction::None;
		switch (pending) {
			case PendingAction::Navigate:
				if (m_session.pending_path != m_session.directory) {
					if (m_session.pending_path.empty()) {
						m_session.directory.clear();
						m_session.options.initial_file_name.clear();
						RebuildContent();
						break;
					}
					std::error_code error;
					auto const target =
					    std::filesystem::weakly_canonical(m_session.pending_path, error);
					if (error || target == m_session.directory ||
					    !std::filesystem::is_directory(target, error)) {
						break;
					}
					m_session.directory = target;
					m_session.options.initial_file_name.clear();
					RebuildContent();
				}
				break;
			case PendingAction::Select:
				m_session.selected_path = m_session.pending_path;
				if (m_session.mode == FileDialogMode::Save) {
					m_tree->GetNode(m_session.file_name_id).AsWidget<TextBox>().text =
					    PathText(m_session.pending_path.filename());
				}
				for (auto const& [node_id, path] : m_session.visible_entries) {
					m_tree->GetNode(node_id).AsWidget<FileItem>().selected =
					    path == m_session.pending_path;
				}
				break;
			case PendingAction::Accept: {
				if (m_session.directory.empty())
					break;
				if (m_session.mode == FileDialogMode::Open) {
					std::error_code error;
					if (!std::filesystem::is_regular_file(m_session.selected_path, error))
						break;
					Complete(m_session.selected_path);
					break;
				}
				auto const& name = m_tree->GetNode(m_session.file_name_id).AsWidget<TextBox>().text;
				if (name.empty())
					break;
				auto path = m_session.directory / name;
				std::error_code error;
				if (path.extension().empty() &&
				    !m_session.options.filters.empty() &&
				    !m_session.options.filters.front().extensions.empty()) {
					auto extension = m_session.options.filters.front().extensions.front();
					if (!extension.empty() && extension.front() != '.')
						extension.insert(extension.begin(), '.');
					path += extension;
				}
				Complete(path);
				break;
			}
			case PendingAction::Cancel:
				Complete({});
				break;
			case PendingAction::None:
				break;
		}
	}

	void FileDialogue::Complete(std::filesystem::path const& path) {
		m_message_box.Close();
		auto callback = std::move(m_session.callback);
		auto const window_id = m_session.window_id;
		m_session = {};
		m_open = false;
		m_host->Close(window_id);
		if (callback)
			callback(path);
	}

	void FileDialogue::Cancel() {
		if (m_message_box.IsOpen()) {
			m_message_box.Close();
			return;
		}
		if (m_open)
			Complete({});
	}

	bool FileDialogue::IsOpen() const noexcept {
		return m_open;
	}

	bool FileDialogue::OwnsWindow(std::uint64_t node_id) const noexcept {
		return (m_open && m_session.window_id == node_id) || m_message_box.OwnsWindow(node_id);
	}

	bool FileDialogue::SetInteraction(std::uint64_t node_id, InteractionState state) {
		if (!m_open)
			return m_message_box.SetInteraction(node_id, state);
		if (m_message_box.SetInteraction(node_id, state))
			return true;
		if (std::ranges::contains(m_session.button_ids, node_id)) {
			auto& button = m_tree->GetNode(node_id).AsWidget<Button>();
			button.interaction = button.enabled ? state : InteractionState::Normal;
			return true;
		}
		for (auto const& [entry_id, path] : m_session.visible_entries) {
			if (entry_id == node_id) {
				m_tree->GetNode(node_id).AsWidget<FileItem>().interaction = state;
				return true;
			}
		}
		return false;
	}

	void FileDialogue::ClearInteractions() {
		if (!m_open)
			return;
		m_message_box.ClearInteraction();
		m_tree->GetNode(m_session.window_id).AsWidget<Window>().non_client_button_interaction =
		    InteractionState::Normal;
		for (auto const node_id : m_session.button_ids) {
			m_tree->GetNode(node_id).AsWidget<Button>().interaction = InteractionState::Normal;
		}
		for (auto const& [node_id, path] : m_session.visible_entries) {
			m_tree->GetNode(node_id).AsWidget<FileItem>().interaction = InteractionState::Normal;
		}
	}

	bool FileDialogue::OwnsTextBox(std::uint64_t node_id) const noexcept {
		return m_open && !m_message_box.IsOpen() && (node_id == m_session.path_id ||
		    (m_session.file_name_id != 0u && node_id == m_session.file_name_id));
	}

	void FileDialogue::SetTextBoxFocused(std::uint64_t node_id, bool focused) {
		if (!OwnsTextBox(node_id))
			return;
		auto node = m_tree->GetNode(node_id);
		if (focused) {
			m_events->Focus(*m_tree, node);
		} else if (m_events->IsFocused(node)) {
			m_events->ClearFocus(*m_tree);
		}
	}

	void FileDialogue::CommitTextBox(std::uint64_t node_id) {
		if (!m_open || node_id != m_session.path_id)
			return;
		auto const& text = m_tree->GetNode(node_id).AsWidget<TextBox>().text;
		auto const utf8_text = text |
		    std::views::transform([](char code_unit) {
			    return static_cast<char8_t>(code_unit);
		    }) |
		    std::ranges::to<std::u8string>();
		std::filesystem::path const requested{utf8_text};
		std::error_code error;
		auto const target = std::filesystem::weakly_canonical(requested, error);
		if (error || !std::filesystem::is_directory(target, error)) {
			m_message_box.Show("Invalid path", "The specified directory does not exist.");
			return;
		}
		if (target == m_session.directory)
			return;
		m_session.directory = target;
		m_session.options.initial_file_name.clear();
		RebuildContent();
	}

} // namespace fyuu_ui
