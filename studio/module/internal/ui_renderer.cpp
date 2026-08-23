module;
#include <version>
#if !defined(__cpp_lib_modules)
// C++98
#include <cstddef>
#include <exception>
#include <memory>
#include <utility>
#include <vector>
#include <algorithm>
#include <cmath>
// C++11
#include <cstdint>
#include <array>
#include <condition_variable>
#include <mutex>
// C++17
#include <optional>
#include <variant>
// C++20
#include <span>
#endif // !defined(__cpp_lib_modules)
#include <ft2build.h>
#include FT_FREETYPE_H

module fyuu_studio:ui_renderer;

import fyuu_engine;
import fyuu_rhi;
import fyuu_ui;
import :rhi_context;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {

}

namespace fyuu_studio {

	struct UIVertex {
		float position_x = 0.0f;
		float position_y = 0.0f;
		float texture_x = 0.0f;
		float texture_y = 0.0f;
		std::uint32_t color = 0u;
	};

	struct UIDrawCommand {
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
		std::uint32_t index_count = 0u;
		std::uint32_t first_index = 0u;
		std::int32_t vertex_offset = 0;
	};

	struct UIDrawData {
		std::vector<UIVertex> vertices;
		std::vector<std::uint32_t> indices;
		std::vector<UIDrawCommand> commands;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
	};

	using CommandGraphResources = fyuu_rhi::execution::CommandGraphResources;
	using Pipeline = fyuu_rhi::Pipeline;
	using PipelineResourceGroup = fyuu_rhi::PipelineResourceGroup;
	using Resource = fyuu_rhi::Resource;
	using Sampler = fyuu_rhi::Sampler;
	using TextureView = fyuu_rhi::View;

	struct FontAtlas {
		static constexpr std::uint32_t CellSize = 48u;
		static constexpr std::uint32_t ColumnCount = 16u;
		static constexpr std::uint32_t RowCount = 6u;
		static constexpr std::uint32_t FontCount = 9u;
		static constexpr std::uint32_t Width = CellSize * ColumnCount;
		static constexpr std::uint32_t Height = CellSize * RowCount * FontCount;

		std::vector<std::uint8_t> coverage;
		std::array<std::uint32_t, FontCount> pixel_sizes{
			12u,
			14u,
			16u,
			18u,
			20u,
			21u,
			24u,
			28u,
			32u
		};
		std::array<std::array<float, 96u>, FontCount> advances;
	};

	FontAtlas CreateFontAtlas() {
		FontAtlas result;
		result.coverage.resize(FontAtlas::Width * FontAtlas::Height);
		FT_Library library = nullptr;
		if (FT_Init_FreeType(&library) != 0) {
			throw std::runtime_error("FreeType initialization failed");
		}
		FT_Face face = nullptr;
#if defined(_WIN32)
		static constexpr std::array FontPaths{ "C:/Windows/Fonts/segoeui.ttf" };
#elif defined(__APPLE__)
		static constexpr std::array FontPaths{
			"/System/Library/Fonts/SFNS.ttf",
			"/System/Library/Fonts/Supplemental/Arial.ttf"
		};
#else
		static constexpr std::array FontPaths{
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"
		};
#endif
		for (auto const* path : FontPaths) {
			if (FT_New_Face(library, path, 0, &face) == 0) {
				break;
			}
		}
		if (face == nullptr) {
			FT_Done_FreeType(library);
			throw std::runtime_error("FreeType could not load the Studio system font");
		}
		for (std::uint32_t font_index = 0u; font_index < FontAtlas::FontCount; ++font_index) {
			if (FT_Set_Pixel_Sizes(face, 0u, result.pixel_sizes[font_index]) != 0) {
				FT_Done_Face(face);
				FT_Done_FreeType(library);
				throw std::runtime_error("FreeType could not set the Studio font size");
			}
			auto const baseline = static_cast<std::int32_t>(face->size->metrics.ascender >> 6u) + 1;
			for (auto character = 32u; character < 128u; ++character) {
				auto const glyph = character - 32u;
				if (FT_Load_Char(face, character, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
					continue;
				}
				auto const cell_x = static_cast<std::int32_t>(
					glyph % FontAtlas::ColumnCount * FontAtlas::CellSize
				);
				auto const cell_y = static_cast<std::int32_t>(
					font_index * FontAtlas::RowCount * FontAtlas::CellSize +
					glyph / FontAtlas::ColumnCount * FontAtlas::CellSize
				);
				auto const target_x = cell_x + 2 + face->glyph->bitmap_left;
				auto const target_y = cell_y + baseline - face->glyph->bitmap_top;
				auto const& bitmap = face->glyph->bitmap;
				for (std::uint32_t row = 0u; row < bitmap.rows; ++row) {
					for (std::uint32_t column = 0u; column < bitmap.width; ++column) {
						auto const x = target_x + static_cast<std::int32_t>(column);
						auto const y = target_y + static_cast<std::int32_t>(row);
						if (x < cell_x || x >= cell_x + static_cast<std::int32_t>(FontAtlas::CellSize) ||
							y < cell_y || y >= cell_y + static_cast<std::int32_t>(FontAtlas::CellSize)) {
							continue;
						}
						auto const source = static_cast<std::size_t>(row) *
							static_cast<std::size_t>(bitmap.pitch) + column;
						auto const target = static_cast<std::size_t>(y) * FontAtlas::Width +
							static_cast<std::size_t>(x);
						result.coverage[target] = bitmap.buffer[source];
					}
				}
				result.advances[font_index][glyph] =
					static_cast<float>(face->glyph->advance.x >> 6u);
			}
		}
		result.coverage[0u] = 255u;
		FT_Done_Face(face);
		FT_Done_FreeType(library);
		return result;
	}

	FontAtlas const& GetFontAtlas() {
		static FontAtlas const Atlas = CreateFontAtlas();
		return Atlas;
	}

	std::uint32_t PackColor(fyuu_ui::Color const& color) {
		auto PackChannel = [](float channel) {
			return static_cast<std::uint32_t>(
				std::round(std::clamp(channel, 0.0f, 1.0f) * 255.0f)
			);
		};
		return PackChannel(color.red) |
			PackChannel(color.green) << 8u |
			PackChannel(color.blue) << 16u |
			PackChannel(color.alpha) << 24u;
	}

	fyuu_ui::Rect Intersect(fyuu_ui::Rect const& left, fyuu_ui::Rect const& right) {
		auto const x = (std::max)(left.position.x, right.position.x);
		auto const y = (std::max)(left.position.y, right.position.y);
		auto const right_edge = (std::min)(
			left.position.x + left.size.width,
			right.position.x + right.size.width
		);
		auto const bottom_edge = (std::min)(
			left.position.y + left.size.height,
			right.position.y + right.size.height
		);
		return fyuu_ui::Rect{
			{ x, y },
			{ (std::max)(0.0f, right_edge - x), (std::max)(0.0f, bottom_edge - y) }
		};
	}

	UIDrawData BuildDrawData(
		std::vector<fyuu_ui::DrawCommand> const& draw_commands,
		std::uint32_t logical_width,
		std::uint32_t logical_height,
		std::uint32_t pixel_width,
		std::uint32_t pixel_height
	) {
		UIDrawData result;
		result.width = pixel_width;
		result.height = pixel_height;
		if (logical_width == 0u || logical_height == 0u ||
			pixel_width == 0u || pixel_height == 0u) {
			return result;
		}
		auto const scale_x = static_cast<float>(pixel_width) / static_cast<float>(logical_width);
		auto const scale_y = static_cast<float>(pixel_height) / static_cast<float>(logical_height);
		std::vector<fyuu_ui::Point> translations{ fyuu_ui::Point{} };
		std::vector<fyuu_ui::Rect> clips{
			fyuu_ui::Rect{ {}, { static_cast<float>(logical_width), static_cast<float>(logical_height) } }
		};
		auto append_quad = [&result, logical_width, logical_height](
			fyuu_ui::Rect const& bounds,
			fyuu_ui::Color const& fill,
			fyuu_ui::Rect const& texture
		) {
			auto const left = bounds.position.x / static_cast<float>(logical_width) * 2.0f - 1.0f;
			auto const right = (bounds.position.x + bounds.size.width) /
				static_cast<float>(logical_width) * 2.0f - 1.0f;
			auto const top = 1.0f - bounds.position.y / static_cast<float>(logical_height) * 2.0f;
			auto const bottom = 1.0f - (bounds.position.y + bounds.size.height) /
				static_cast<float>(logical_height) * 2.0f;
			auto const color = PackColor(fill);
			auto const first_vertex = static_cast<std::uint32_t>(result.vertices.size());
			result.vertices.emplace_back(UIVertex{ left, top, texture.position.x, texture.position.y, color });
			result.vertices.emplace_back(UIVertex{ right, top, texture.size.width, texture.position.y, color });
			result.vertices.emplace_back(UIVertex{ right, bottom, texture.size.width, texture.size.height, color });
			result.vertices.emplace_back(UIVertex{ left, bottom, texture.position.x, texture.size.height, color });
			result.indices.emplace_back(first_vertex);
			result.indices.emplace_back(first_vertex + 1u);
			result.indices.emplace_back(first_vertex + 2u);
			result.indices.emplace_back(first_vertex);
			result.indices.emplace_back(first_vertex + 2u);
			result.indices.emplace_back(first_vertex + 3u);
		};
		for (auto const& command : draw_commands) {
			std::visit(
				[&append_quad, &clips, &result, &translations, logical_width, logical_height, scale_x, scale_y](auto const& state) {
					using State = std::remove_cvref_t<decltype(state)>;
					if constexpr (std::same_as<State, fyuu_ui::PushTransformCommand>) {
						auto const& parent = translations.back();
						translations.emplace_back(fyuu_ui::Point{
							parent.x + state.transform.translation.x,
							parent.y + state.transform.translation.y
						});
					}
					else if constexpr (std::same_as<State, fyuu_ui::PopTransformCommand>) {
						translations.pop_back();
					}
					else if constexpr (std::same_as<State, fyuu_ui::PushClipCommand>) {
						auto clip = state.bounds;
						clip.position.x += translations.back().x;
						clip.position.y += translations.back().y;
						clips.emplace_back(Intersect(clips.back(), clip));
					}
					else if constexpr (std::same_as<State, fyuu_ui::PopClipCommand>) {
						clips.pop_back();
					}
					else if constexpr (std::same_as<State, fyuu_ui::DrawRectangleCommand>) {
						auto bounds = state.bounds;
						bounds.position.x += translations.back().x;
						bounds.position.y += translations.back().y;
						auto const left = bounds.position.x / static_cast<float>(logical_width) * 2.0f - 1.0f;
						auto const right = (bounds.position.x + bounds.size.width) /
							static_cast<float>(logical_width) * 2.0f - 1.0f;
						auto const top = 1.0f - bounds.position.y / static_cast<float>(logical_height) * 2.0f;
						auto const bottom = 1.0f - (bounds.position.y + bounds.size.height) /
							static_cast<float>(logical_height) * 2.0f;
						auto const color = PackColor(state.fill);
						auto const first_vertex = static_cast<std::uint32_t>(result.vertices.size());
						auto const first_index = static_cast<std::uint32_t>(result.indices.size());
						result.vertices.emplace_back(UIVertex{ left, top, 0.0f, 0.0f, color });
						result.vertices.emplace_back(UIVertex{ right, top, 0.0f, 0.0f, color });
						result.vertices.emplace_back(UIVertex{ right, bottom, 0.0f, 0.0f, color });
						result.vertices.emplace_back(UIVertex{ left, bottom, 0.0f, 0.0f, color });
						result.indices.emplace_back(first_vertex);
						result.indices.emplace_back(first_vertex + 1u);
						result.indices.emplace_back(first_vertex + 2u);
						result.indices.emplace_back(first_vertex);
						result.indices.emplace_back(first_vertex + 2u);
						result.indices.emplace_back(first_vertex + 3u);
						auto const& clip = clips.back();
						result.commands.push_back(
							{
								static_cast<std::int32_t>((std::max)(clip.position.x * scale_x, 0.0f)),
								static_cast<std::int32_t>((std::max)(clip.position.y * scale_y, 0.0f)),
								static_cast<std::uint32_t>((std::max)(clip.size.width * scale_x, 0.0f)),
								static_cast<std::uint32_t>((std::max)(clip.size.height * scale_y, 0.0f)),
								6u,
								first_index,
								0
							}
						);
					}
					else if constexpr (std::same_as<State, fyuu_ui::DrawGradientRectangleCommand>) {
						auto bounds = state.bounds;
						bounds.position.x += translations.back().x;
						bounds.position.y += translations.back().y;
						auto const left = bounds.position.x / static_cast<float>(logical_width) * 2.0f - 1.0f;
						auto const right = (bounds.position.x + bounds.size.width) /
							static_cast<float>(logical_width) * 2.0f - 1.0f;
						auto const top = 1.0f - bounds.position.y / static_cast<float>(logical_height) * 2.0f;
						auto const bottom = 1.0f - (bounds.position.y + bounds.size.height) /
							static_cast<float>(logical_height) * 2.0f;
						auto const top_color = PackColor(state.top);
						auto const bottom_color = PackColor(state.bottom);
						auto const first_vertex = static_cast<std::uint32_t>(result.vertices.size());
						auto const first_index = static_cast<std::uint32_t>(result.indices.size());
						result.vertices.emplace_back(UIVertex{ left, top, 0.0f, 0.0f, top_color });
						result.vertices.emplace_back(UIVertex{ right, top, 0.0f, 0.0f, top_color });
						result.vertices.emplace_back(UIVertex{ right, bottom, 0.0f, 0.0f, bottom_color });
						result.vertices.emplace_back(UIVertex{ left, bottom, 0.0f, 0.0f, bottom_color });
						result.indices.emplace_back(first_vertex);
						result.indices.emplace_back(first_vertex + 1u);
						result.indices.emplace_back(first_vertex + 2u);
						result.indices.emplace_back(first_vertex);
						result.indices.emplace_back(first_vertex + 2u);
						result.indices.emplace_back(first_vertex + 3u);
						auto const& clip = clips.back();
						result.commands.emplace_back(UIDrawCommand{
							static_cast<std::int32_t>((std::max)(clip.position.x * scale_x, 0.0f)),
							static_cast<std::int32_t>((std::max)(clip.position.y * scale_y, 0.0f)),
							static_cast<std::uint32_t>((std::max)(clip.size.width * scale_x, 0.0f)),
							static_cast<std::uint32_t>((std::max)(clip.size.height * scale_y, 0.0f)),
							6u,
							first_index,
							0
						});
					}
					else if constexpr (std::same_as<State, fyuu_ui::DrawLineCommand>) {
						auto start = state.start;
						auto end = state.end;
						start.x += translations.back().x;
						start.y += translations.back().y;
						end.x += translations.back().x;
						end.y += translations.back().y;
						auto const delta_x = end.x - start.x;
						auto const delta_y = end.y - start.y;
						auto const length = std::sqrt(delta_x * delta_x + delta_y * delta_y);
						if (length <= 0.0f) {
							return;
						}
						auto const normal_x = -delta_y / length * state.thickness * 0.5f;
						auto const normal_y = delta_x / length * state.thickness * 0.5f;
						std::array const points{
							fyuu_ui::Point{ start.x + normal_x, start.y + normal_y },
							fyuu_ui::Point{ end.x + normal_x, end.y + normal_y },
							fyuu_ui::Point{ end.x - normal_x, end.y - normal_y },
							fyuu_ui::Point{ start.x - normal_x, start.y - normal_y }
						};
						auto const color = PackColor(state.color);
						auto const first_vertex = static_cast<std::uint32_t>(result.vertices.size());
						auto const first_index = static_cast<std::uint32_t>(result.indices.size());
						for (auto const& point : points) {
							result.vertices.emplace_back(UIVertex{
								point.x / static_cast<float>(logical_width) * 2.0f - 1.0f,
								1.0f - point.y / static_cast<float>(logical_height) * 2.0f,
								0.0f,
								0.0f,
								color
							});
						}
						result.indices.emplace_back(first_vertex);
						result.indices.emplace_back(first_vertex + 1u);
						result.indices.emplace_back(first_vertex + 2u);
						result.indices.emplace_back(first_vertex);
						result.indices.emplace_back(first_vertex + 2u);
						result.indices.emplace_back(first_vertex + 3u);
						auto const& clip = clips.back();
						result.commands.emplace_back(UIDrawCommand{
							static_cast<std::int32_t>((std::max)(clip.position.x * scale_x, 0.0f)),
							static_cast<std::int32_t>((std::max)(clip.position.y * scale_y, 0.0f)),
							static_cast<std::uint32_t>((std::max)(clip.size.width * scale_x, 0.0f)),
							static_cast<std::uint32_t>((std::max)(clip.size.height * scale_y, 0.0f)),
							6u,
							first_index,
							0
						});
					}
					else if constexpr (std::same_as<State, fyuu_ui::DrawTextCommand>) {
						auto const& atlas = GetFontAtlas();
						auto const physical_font_size = state.font_size * scale_y;
						std::uint32_t font_index = 0u;
						for (std::uint32_t index = 1u; index < FontAtlas::FontCount; ++index) {
							if (std::abs(static_cast<float>(atlas.pixel_sizes[index]) - physical_font_size) <
								std::abs(static_cast<float>(atlas.pixel_sizes[font_index]) - physical_font_size)) {
								font_index = index;
							}
						}
						auto caret_advance = 0.0f;
						if (state.caret_offset.has_value()) {
							for (std::size_t offset = 0u;
								offset < (std::min)(*state.caret_offset, state.text.size());
								++offset) {
								auto const code = static_cast<std::uint8_t>(state.text[offset]);
								if (code < 32u || code >= 128u) {
									caret_advance += std::round(state.font_size * 0.58f * scale_x) /
										scale_x;
									continue;
								}
								auto const glyph = static_cast<std::uint32_t>(code - 32u);
								caret_advance += atlas.advances[font_index][glyph] / scale_x;
							}
						}
						auto const caret_width = 1.0f / scale_x;
						auto const horizontal_scroll = std::max(
							0.0f,
							caret_advance - std::max(0.0f, state.bounds.size.width - caret_width)
						);
						auto cursor_x = std::round(
							(state.bounds.position.x + translations.back().x) * scale_x
						) / scale_x - horizontal_scroll;
						auto const cursor_y = std::round(
							(state.bounds.position.y + translations.back().y) * scale_y
						) / scale_y;
						auto const first_index = static_cast<std::uint32_t>(result.indices.size());
						for (std::size_t offset = 0u; offset <= state.text.size(); ++offset) {
							if (state.caret_offset == offset) {
								auto const caret_x = (std::min)(
									cursor_x,
									state.bounds.position.x + translations.back().x +
										state.bounds.size.width - caret_width
								);
								append_quad(
									fyuu_ui::Rect{
										{ caret_x, cursor_y },
										{
											caret_width,
											static_cast<float>(atlas.pixel_sizes[font_index]) / scale_y
										}
									},
									state.color,
									fyuu_ui::Rect{}
								);
							}
							if (offset == state.text.size()) {
								break;
							}
							auto const character = state.text[offset];
							auto const code = static_cast<std::uint8_t>(character);
							if (code < 32u || code >= 128u) {
								cursor_x += std::round(state.font_size * 0.58f * scale_x) / scale_x;
								continue;
							}
							auto const glyph = static_cast<std::uint32_t>(code - 32u);
							auto const atlas_x = static_cast<float>(
								glyph % FontAtlas::ColumnCount * FontAtlas::CellSize
							);
							auto const atlas_y = static_cast<float>(
								font_index * FontAtlas::RowCount * FontAtlas::CellSize +
								glyph / FontAtlas::ColumnCount * FontAtlas::CellSize
							);
							append_quad(
								fyuu_ui::Rect{
									{ cursor_x, cursor_y },
									{
										FontAtlas::CellSize / scale_x,
										FontAtlas::CellSize / scale_y
									}
								},
								state.color,
								fyuu_ui::Rect{
									{ atlas_x / FontAtlas::Width, atlas_y / FontAtlas::Height },
									{
										(atlas_x + FontAtlas::CellSize) / FontAtlas::Width,
										(atlas_y + FontAtlas::CellSize) / FontAtlas::Height
									}
								}
							);
							cursor_x += atlas.advances[font_index][glyph] / scale_x;
						}
						auto const index_count = static_cast<std::uint32_t>(result.indices.size()) - first_index;
						if (index_count != 0u) {
							auto text_bounds = state.bounds;
							text_bounds.position.x += translations.back().x;
							text_bounds.position.y += translations.back().y;
							auto const clip = Intersect(clips.back(), text_bounds);
							result.commands.push_back(
								UIDrawCommand{
									static_cast<std::int32_t>((std::max)(clip.position.x * scale_x, 0.0f)),
									static_cast<std::int32_t>((std::max)(clip.position.y * scale_y, 0.0f)),
									static_cast<std::uint32_t>((std::max)(clip.size.width * scale_x, 0.0f)),
									static_cast<std::uint32_t>((std::max)(clip.size.height * scale_y, 0.0f)),
									index_count,
									first_index,
									0
								}
							);
						}
					}
				},
				command
			);
		}
		return result;
	}

	struct UploadState {
		std::mutex mutex;
		std::condition_variable condition;
		std::optional<CommandGraphResources> resources;
		std::exception_ptr error;
		bool done = false;
		bool stopped = false;

		CommandGraphResources Wait() {
			std::unique_lock lock{ mutex };
			auto completed = [this] {
				return done;
			};
			condition.wait(lock, completed);
			if (error) {
				std::rethrow_exception(error);
			}
			if (stopped || !resources) {
				throw fyuu_engine::Error{
					fyuu_engine::Result::ApplicationError,
					"UI resource upload was cancelled"
				};
			}
			auto result = std::move(*resources);
			resources.reset();
			return result;
		}

		bool TryTake(std::optional<CommandGraphResources>& output) {
			std::lock_guard lock{ mutex };
			if (!done) {
				return false;
			}
			if (error) {
				std::rethrow_exception(error);
			}
			if (stopped || !resources) {
				throw fyuu_engine::Error{
					fyuu_engine::Result::ApplicationError,
					"UI geometry upload was cancelled"
				};
			}
			output.emplace(std::move(*resources));
			resources.reset();
			return true;
		}
	};

	struct UploadReceiver {
		std::shared_ptr<UploadState> state;

		struct Environment {
		};

		Environment get_env() const noexcept {
			return {};
		}

		void RecoverBindings(CommandGraphResources&& resources) noexcept {
			std::lock_guard lock{ state->mutex };
			state->resources.emplace(std::move(resources));
		}

		void set_value(CommandGraphResources&& resources) && noexcept {
			{
				std::lock_guard lock{ state->mutex };
				state->resources.emplace(std::move(resources));
				state->done = true;
			}
			state->condition.notify_one();
		}

		void set_error(std::exception_ptr error) && noexcept {
			{
				std::lock_guard lock{ state->mutex };
				state->error = std::move(error);
				state->done = true;
			}
			state->condition.notify_one();
		}

		void set_stopped() && noexcept {
			{
				std::lock_guard lock{ state->mutex };
				state->stopped = true;
				state->done = true;
			}
			state->condition.notify_one();
		}
	};

	struct RenderFrame {
		std::optional<Resource> target;
		std::optional<TextureView> target_view;
		std::optional<Pipeline> pipeline;
		std::optional<Resource> font_texture;
		std::optional<TextureView> font_view;
		std::optional<Sampler> font_sampler;
		std::optional<PipelineResourceGroup> font_group;
		std::optional<Resource> vertex_buffer;
		std::optional<Resource> index_buffer;
		std::shared_ptr<UploadState> submission;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
		std::size_t vertex_capacity = 0u;
		std::size_t index_capacity = 0u;
		std::size_t target_binding = 0u;
		std::size_t target_view_binding = 0u;
		std::size_t pipeline_binding = 0u;
		std::size_t font_texture_binding = 0u;
		std::size_t font_view_binding = 0u;
		std::size_t font_sampler_binding = 0u;
		std::size_t font_group_binding = 0u;
		std::optional<std::size_t> vertex_binding;
		std::optional<std::size_t> index_binding;
	};

	auto CreateUIPipeline(RHIContext& context) {
		using namespace fyuu_rhi::pipeline;
		static constexpr char CombinedShaderSource[] = R"(
			Sampler2D font_atlas : register(t0, space0);
			struct VSOut {
				float4 position : SV_Position;
				float2 uv : TEXCOORD0;
				float4 color : COLOR0;
			};
			[shader("vertex")]
			VSOut vs_main(float2 position : POSITION, float2 uv : TEXCOORD0, float4 color : COLOR0) {
				VSOut output;
				output.position = float4(position, 0.0, 1.0);
				output.uv = uv;
				output.color = color;
				return output;
			}
			[shader("fragment")]
			float4 fs_main(VSOut input) : SV_Target0 {
				return input.color * font_atlas.Sample(input.uv).r;
			}
		)";
		std::array modules{
			SlangPipelineProgramDescriptor::Module{
				.name = "fyuu_ui",
				.source = CombinedShaderSource
			}
		};
		std::array entry_points{
			SlangPipelineProgramDescriptor::EntryPoint{
				.name = "vs_main",
				.stage = Stage::Vertex
			},
			SlangPipelineProgramDescriptor::EntryPoint{
				.name = "fs_main",
				.stage = Stage::Fragment
			}
		};
		std::array buffers{
			VertexBufferLayout{
				.slot = 0u,
				.stride = sizeof(UIVertex)
			}
		};
		std::array attributes{
			VertexAttribute{
				.location = 0u,
				.slot = 0u,
				.offset = 0u,
				.format = fyuu_rhi::ResourceFlagBits::R32G32Float
			},
			VertexAttribute{
				.location = 1u,
				.slot = 0u,
				.offset = 8u,
				.format = fyuu_rhi::ResourceFlagBits::R32G32Float
			},
			VertexAttribute{
				.location = 2u,
				.slot = 0u,
				.offset = 16u,
				.format = fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm
			}
		};
		std::array color_targets{
			ColorTargetState{
				.format = fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm,
				.blend = BlendState{
					.color = {
						BlendFactor::SourceAlpha,
						BlendFactor::OneMinusSourceAlpha,
						BlendOperation::Add
					},
					.alpha = {
						BlendFactor::One,
						BlendFactor::OneMinusSourceAlpha,
						BlendOperation::Add
					}
				}
			}
		};
		return context.GetLogicalDevice().CreateGraphicsPipeline(
			{
				.program = {
					.modules = modules,
					.entry_points = entry_points
				},
				.vertex = {
					.buffers = buffers,
					.attributes = attributes
				},
				.color_targets = color_targets
			}
		);
	}

	/// Owns three complete frame sets so move-only RHI bindings can remain in flight.
	/// Call chain: StudioApplication::Tick -> Submit -> upload -> render -> present.
	class UIRenderer final {
	private:
		using Frame = RenderFrame;

		RHIContext* m_context = nullptr;
		std::array<Frame, 3u> m_frames;
		std::size_t m_next_frame = 0u;

		static fyuu_rhi::ResourceFlags PresentationTextureFlags() {
			fyuu_rhi::ResourceFlags flags;
			flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
			flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
			flags.Set(fyuu_rhi::ResourceFlagBits::TextureView2D);
			flags.Set(fyuu_rhi::ResourceFlagBits::RenderAttachment);
			flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
			flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);
			return flags;
		}

		static fyuu_rhi::ResourceFlags GeometryBufferFlags(fyuu_rhi::ResourceFlagBits usage) {
			fyuu_rhi::ResourceFlags flags;
			flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
			flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
			flags.Set(usage);
			return flags;
		}

		static fyuu_rhi::ResourceFlags FontTextureFlags() {
			fyuu_rhi::ResourceFlags flags;
			flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
			flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
			flags.Set(fyuu_rhi::ResourceFlagBits::TextureView2D);
			flags.Set(fyuu_rhi::ResourceFlagBits::TextureBinding);
			flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
			flags.Set(fyuu_rhi::ResourceFlagBits::R8Unorm);
			return flags;
		}

		static fyuu_rhi::ResourceFlags StagingBufferFlags() {
			fyuu_rhi::ResourceFlags flags;
			flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
			flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
			flags.Set(fyuu_rhi::ResourceFlagBits::HostVisible);
			return flags;
		}

		void Restore(
			Frame& frame,
			CommandGraphResources&& resources
		) {
			frame.target.emplace(resources.TakeResource(frame.target_binding));
			frame.target_view.emplace(resources.TakeView(frame.target_view_binding));
			frame.pipeline.emplace(resources.TakePipeline(frame.pipeline_binding));
			frame.font_texture.emplace(resources.TakeResource(frame.font_texture_binding));
			frame.font_view.emplace(resources.TakeView(frame.font_view_binding));
			frame.font_sampler.emplace(resources.TakeSampler(frame.font_sampler_binding));
			frame.font_group.emplace(resources.TakeResourceGroup(frame.font_group_binding));
			if (frame.vertex_binding) {
				frame.vertex_buffer.emplace(
					resources.TakeResource(*frame.vertex_binding)
				);
			}
			if (frame.index_binding) {
				frame.index_buffer.emplace(
					resources.TakeResource(*frame.index_binding)
				);
			}
			frame.submission.reset();
		}

		bool Recover(Frame& frame) {
			if (!frame.submission) {
				return true;
			}
			std::optional<CommandGraphResources> resources;
			if (!frame.submission->TryTake(resources)) {
				return false;
			}
			Restore(frame, std::move(*resources));
			return true;
		}

		void Drain() noexcept {
			for (auto& frame : m_frames) {
				if (!frame.submission) {
					continue;
				}
				try {
					auto resources = frame.submission->Wait();
					Restore(frame, std::move(resources));
				}
				catch (...) {
					frame.submission.reset();
				}
			}
		}

		Frame* AcquireFrame() {
			for (std::size_t offset = 0u; offset < m_frames.size(); ++offset) {
				auto const index = (m_next_frame + offset) % m_frames.size();
				auto& frame = m_frames[index];
				if (Recover(frame)) {
					m_next_frame = (index + 1u) % m_frames.size();
					return &frame;
				}
			}
			return nullptr;
		}

		void InitializeFrame(Frame& frame) {
			frame.pipeline.emplace(CreateUIPipeline(*m_context));
			frame.font_texture.emplace(
				m_context->GetLogicalDevice().CreateTexture(
					FontAtlas::Width,
					FontAtlas::Height,
					1u,
					1u,
					FontTextureFlags()
				)
			);
			frame.font_view.emplace(
				frame.font_texture->CreateTextureView(
					0u,
					1u,
					0u,
					1u,
					FontTextureFlags()
				)
			);
			frame.font_sampler.emplace(
				m_context->GetLogicalDevice().CreateSampler({
					.address_mode_u = fyuu_rhi::AddressMode::ClampToEdge,
					.address_mode_v = fyuu_rhi::AddressMode::ClampToEdge,
					.address_mode_w = fyuu_rhi::AddressMode::ClampToEdge,
					.mag_filter = fyuu_rhi::FilterMode::Linear,
					.min_filter = fyuu_rhi::FilterMode::Linear,
					.mipmap_filter = fyuu_rhi::MipmapFilterMode::Nearest,
					.max_lod = 0.0f
				})
			);
			using BindingValue = fyuu_rhi::pipeline::BindingValue;
			using ResourceBinding = fyuu_rhi::pipeline::ResourceBinding;
			std::array bindings{
				ResourceBinding{
					.slot = 0u,
					.value = BindingValue::FromCombined(*frame.font_view, *frame.font_sampler)
				}
			};
			frame.font_group.emplace(
				frame.pipeline->CreatePipelineResourceGroup(0u, bindings)
			);
		}

		void EnsurePresentationTarget(
			Frame& frame,
			std::uint32_t width,
			std::uint32_t height
		) {
			if (frame.target && frame.width == width && frame.height == height) {
				return;
			}
			frame.target.emplace(
				m_context->GetLogicalDevice().CreateTexture(
					width,
					height,
					1u,
					1u,
					PresentationTextureFlags()
				)
			);
			frame.target_view.emplace(
				frame.target->CreateTextureView(
					0u,
					1u,
					0u,
					1u,
					PresentationTextureFlags()
				)
			);
			frame.width = width;
			frame.height = height;
		}

		void EnsureGeometryCapacity(
			Frame& frame,
			std::size_t vertex_bytes,
			std::size_t index_bytes
		) {
			if (!frame.vertex_buffer || frame.vertex_capacity < vertex_bytes) {
				frame.vertex_buffer.emplace(
					m_context->GetLogicalDevice().CreateBuffer(
						vertex_bytes,
						GeometryBufferFlags(fyuu_rhi::ResourceFlagBits::VertexBuffer)
					)
				);
				frame.vertex_capacity = vertex_bytes;
			}
			if (!frame.index_buffer || frame.index_capacity < index_bytes) {
				frame.index_buffer.emplace(
					m_context->GetLogicalDevice().CreateBuffer(
						index_bytes,
						GeometryBufferFlags(fyuu_rhi::ResourceFlagBits::IndexBuffer)
					)
				);
				frame.index_capacity = index_bytes;
			}
		}

		void SubmitFrame(
			Frame& frame,
			UIDrawData const& draw_data,
			std::span<std::byte const> vertex_data,
			std::span<std::byte const> index_data,
			std::span<std::byte const> atlas_data
		) {
			using namespace fyuu_rhi::execution;
			auto builder = m_context->GetScheduler().schedule();
			frame.target_binding = builder.RegisterResource();
			frame.target_view_binding = builder.RegisterView();
			frame.pipeline_binding = builder.RegisterPipeline();
			frame.font_texture_binding = builder.RegisterResource();
			frame.font_view_binding = builder.RegisterView();
			frame.font_sampler_binding = builder.RegisterSampler();
			frame.font_group_binding = builder.RegisterResourceGroup();
			frame.vertex_binding = builder.RegisterResource();
			frame.index_binding = builder.RegisterResource();
			auto const vertex_staging_binding = builder.RegisterResource();
			auto const index_staging_binding = builder.RegisterResource();
			auto const atlas_staging_binding = builder.RegisterResource();

			auto transfer = builder.CreateNode(QueueType::Transfer);
			transfer
				.Access({ vertex_staging_binding, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ index_staging_binding, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ atlas_staging_binding, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ frame.font_texture_binding, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Access({ *frame.vertex_binding, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Access({ *frame.index_binding, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(WriteBuffer{
					vertex_staging_binding,
					0u,
					std::vector<std::byte>{ vertex_data.begin(), vertex_data.end() }
				})
				.Record(CopyBuffer{
					vertex_staging_binding,
					*frame.vertex_binding,
					0u,
					0u,
					vertex_data.size()
				})
				.Record(WriteBuffer{
					index_staging_binding,
					0u,
					std::vector<std::byte>{ index_data.begin(), index_data.end() }
				})
				.Record(CopyBuffer{
					index_staging_binding,
					*frame.index_binding,
					0u,
					0u,
					index_data.size()
				})
				.Record(WriteBuffer{
					atlas_staging_binding,
					0u,
					std::vector<std::byte>{ atlas_data.begin(), atlas_data.end() }
				})
				.Record(CopyBufferToTexture{
					atlas_staging_binding,
					frame.font_texture_binding,
					{
						.offset = 0u,
						.bytes_per_row = FontAtlas::Width,
						.rows_per_image = FontAtlas::Height
					},
					{
						.array_layer_count = 1u,
						.width = FontAtlas::Width,
						.height = FontAtlas::Height,
						.depth = 1u
					}
				});

			auto render = builder.CreateNode(QueueType::Graphics, transfer);
			render
				.Access({ frame.target_binding, AccessMode::Write, ResourceUsage::ColorAttachment, {} })
				.Access({ *frame.vertex_binding, AccessMode::Read, ResourceUsage::VertexBuffer, {} })
				.Access({ *frame.index_binding, AccessMode::Read, ResourceUsage::IndexBuffer, {} })
				.Access({ frame.font_texture_binding, AccessMode::Read, ResourceUsage::Sampled, {} })
				.Record(BindPipeline{ frame.pipeline_binding })
				.Record(BindResourceGroup{ frame.font_group_binding, 0u })
				.Record(BindVertexBuffer{
					*frame.vertex_binding,
					0u,
					static_cast<std::uint32_t>(sizeof(UIVertex)),
					0u
				})
				.Record(BindIndexBuffer{
					*frame.index_binding,
					IndexType::Uint32,
					0u
				})
				.Record(BeginRendering{
					{ 0, 0, draw_data.width, draw_data.height },
					{{ ColorAttachment{
						frame.target_binding,
						frame.target_view_binding,
						LoadOperation::Clear,
						StoreOperation::Store,
						{ 0.025f, 0.03f, 0.04f, 1.0f },
						std::nullopt,
						std::nullopt
					} }},
					{}
				})
				.Record(Viewport{
					0.0f,
					0.0f,
					static_cast<float>(draw_data.width),
					static_cast<float>(draw_data.height),
					0.0f,
					1.0f,
					fyuu_rhi::execution::ClipSpace::YUp
				});
			for (auto const& command : draw_data.commands) {
				render
					.Record(Scissor{
						command.x,
						command.y,
						command.width,
						command.height
					})
					.Record(DrawIndexed{
						command.index_count,
						1u,
						command.first_index,
						command.vertex_offset,
						0u
					});
			}
			render.Record(EndRendering{});

			auto present = builder.CreateNode(QueueType::Present, render);
			present
				.Access({ frame.target_binding, AccessMode::Read, ResourceUsage::PresentationSource, {} })
				.Record(Present{
					frame.target_binding,
					0u,
					static_cast<std::uint32_t>(m_frames.size()),
					true
				});

			auto vertex_staging = m_context->GetLogicalDevice().CreateBuffer(
				vertex_data.size(),
				StagingBufferFlags()
			);
			auto index_staging = m_context->GetLogicalDevice().CreateBuffer(
				index_data.size(),
				StagingBufferFlags()
			);
			auto atlas_staging = m_context->GetLogicalDevice().CreateBuffer(
				atlas_data.size(),
				StagingBufferFlags()
			);
			frame.submission = std::make_shared<UploadState>();
			auto operation = std::move(builder).connect(
				UploadReceiver{ frame.submission }
			);
			operation.BindResource(frame.target_binding, std::move(*frame.target));
			operation.BindResource(*frame.vertex_binding, std::move(*frame.vertex_buffer));
			operation.BindResource(*frame.index_binding, std::move(*frame.index_buffer));
			operation.BindResource(vertex_staging_binding, std::move(vertex_staging));
			operation.BindResource(index_staging_binding, std::move(index_staging));
			operation.BindResource(frame.font_texture_binding, std::move(*frame.font_texture));
			operation.BindResource(atlas_staging_binding, std::move(atlas_staging));
			operation.BindView(frame.target_view_binding, std::move(*frame.target_view));
			operation.BindView(frame.font_view_binding, std::move(*frame.font_view));
			operation.BindSampler(frame.font_sampler_binding, std::move(*frame.font_sampler));
			operation.BindPipeline(frame.pipeline_binding, std::move(*frame.pipeline));
			operation.BindResourceGroup(frame.font_group_binding, std::move(*frame.font_group));
			operation.SetPresentationTarget(m_context->GetPresentationHandle());
			frame.target.reset();
			frame.target_view.reset();
			frame.pipeline.reset();
			frame.font_texture.reset();
			frame.font_view.reset();
			frame.font_sampler.reset();
			frame.font_group.reset();
			frame.vertex_buffer.reset();
			frame.index_buffer.reset();
			operation.start();
		}

	public:
		explicit UIRenderer(RHIContext& context)
			: m_context(&context) {
			for (auto& frame : m_frames) {
				InitializeFrame(frame);
			}
		}

		~UIRenderer() noexcept {
			Drain();
		}

		void Submit(
			std::vector<fyuu_ui::DrawCommand> const& draw_commands,
			std::uint32_t logical_width,
			std::uint32_t logical_height,
			std::uint32_t pixel_width,
			std::uint32_t pixel_height
		) {
			auto const draw_data = BuildDrawData(
				draw_commands,
				logical_width,
				logical_height,
				pixel_width,
				pixel_height
			);
			if (draw_data.width == 0u || draw_data.height == 0u ||
				draw_data.vertices.empty() || draw_data.indices.empty()) {
				return;
			}
			auto* frame = AcquireFrame();
			if (!frame) {
				return;
			}
			auto const vertex_data = std::as_bytes(std::span{ draw_data.vertices });
			auto const index_data = std::as_bytes(std::span{ draw_data.indices });
			auto const atlas_data = std::as_bytes(std::span{ GetFontAtlas().coverage });
			EnsurePresentationTarget(*frame, draw_data.width, draw_data.height);
			EnsureGeometryCapacity(
				*frame,
				vertex_data.size(),
				index_data.size()
			);
			SubmitFrame(*frame, draw_data, vertex_data, index_data, atlas_data);
		}
	};

}
