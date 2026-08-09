module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <vector>
#endif // !defined(__cpp_lib_modules)

export module fyuu_engine:scene_view;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {

	std::array<float, 4> MultiplyQuaternion(
		std::array<float, 4> const& left,
		std::array<float, 4> const& right
	) {
		return {
			left[3] * right[0] + left[0] * right[3] + left[1] * right[2] - left[2] * right[1],
			left[3] * right[1] - left[0] * right[2] + left[1] * right[3] + left[2] * right[0],
			left[3] * right[2] + left[0] * right[1] - left[1] * right[0] + left[2] * right[3],
			left[3] * right[3] - left[0] * right[0] - left[1] * right[1] - left[2] * right[2]
		};
	}

	std::array<float, 3> RotateVector(
		std::array<float, 4> const& rotation,
		std::array<float, 3> const& vector
	) {
		auto const dot = rotation[0] * vector[0]
			+ rotation[1] * vector[1]
			+ rotation[2] * vector[2];
		auto const rotation_length = rotation[0] * rotation[0]
			+ rotation[1] * rotation[1]
			+ rotation[2] * rotation[2];
		return {
			2.0f * dot * rotation[0]
				+ (rotation[3] * rotation[3] - rotation_length) * vector[0]
				+ 2.0f * rotation[3] * (rotation[1] * vector[2] - rotation[2] * vector[1]),
			2.0f * dot * rotation[1]
				+ (rotation[3] * rotation[3] - rotation_length) * vector[1]
				+ 2.0f * rotation[3] * (rotation[2] * vector[0] - rotation[0] * vector[2]),
			2.0f * dot * rotation[2]
				+ (rotation[3] * rotation[3] - rotation_length) * vector[2]
				+ 2.0f * rotation[3] * (rotation[0] * vector[1] - rotation[1] * vector[0])
		};
	}

}

namespace fyuu_engine {

	export struct SceneViewGeometry {
		std::vector<std::array<float, 3>> positions;
		std::vector<std::uint32_t> indices;

		[[nodiscard]] bool Valid() const noexcept {
			if (positions.empty() || indices.empty() || indices.size() % 3u != 0u) {
				return false;
			}
			return std::ranges::all_of(
				indices,
				[this](std::uint32_t const& index) {
					return index < positions.size();
				}
			);
		}
	};

	export struct SceneViewEntity {
		std::array<float, 3> translation{};
		std::array<float, 4> rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
		std::int64_t parent_index = -1;
		std::size_t geometry_index = std::numeric_limits<std::size_t>::max();
		bool selected = false;
	};

	export struct SceneViewSubmission {
		std::vector<std::shared_ptr<SceneViewGeometry const>> geometries;
		std::vector<SceneViewEntity> entities;
		std::array<float, 2> camera_position{};
		float pixels_per_unit = 48.0f;
	};

	export void ResolveSceneView(SceneViewSubmission& submission) {
		std::vector<bool> resolved(submission.entities.size(), false);
		auto resolved_count = std::size_t{ 0u };
		while (resolved_count < submission.entities.size()) {
			auto progressed = false;
			for (std::size_t entity_index = 0u; entity_index < submission.entities.size(); ++entity_index) {
				if (resolved[entity_index]) {
					continue;
				}
				auto& entity = submission.entities[entity_index];
				auto const parent_index = entity.parent_index;
				if (parent_index < 0
					|| static_cast<std::size_t>(parent_index) >= submission.entities.size()) {
					entity.parent_index = -1;
					resolved[entity_index] = true;
					++resolved_count;
					progressed = true;
					continue;
				}
				auto const parent = static_cast<std::size_t>(parent_index);
				if (!resolved[parent]) {
					continue;
				}
				auto const& parent_entity = submission.entities[parent];
				auto const scaled_translation = std::array{
					entity.translation[0] * parent_entity.scale[0],
					entity.translation[1] * parent_entity.scale[1],
					entity.translation[2] * parent_entity.scale[2]
				};
				auto const rotated_translation = RotateVector(parent_entity.rotation, scaled_translation);
				for (std::size_t component = 0u; component < 3u; ++component) {
					entity.translation[component] = parent_entity.translation[component]
						+ rotated_translation[component];
					entity.scale[component] *= parent_entity.scale[component];
				}
				entity.rotation = MultiplyQuaternion(parent_entity.rotation, entity.rotation);
				entity.parent_index = -1;
				resolved[entity_index] = true;
				++resolved_count;
				progressed = true;
			}
			if (!progressed) {
				for (std::size_t entity_index = 0u; entity_index < submission.entities.size(); ++entity_index) {
					if (!resolved[entity_index]) {
						submission.entities[entity_index].parent_index = -1;
					}
				}
				break;
			}
		}
	}

	export std::array<float, 3> SceneViewLocalVector(
		SceneViewEntity const& parent,
		std::array<float, 3> const& world_vector
	) {
		auto const inverse_rotation = std::array{
			-parent.rotation[0],
			-parent.rotation[1],
			-parent.rotation[2],
			parent.rotation[3]
		};
		auto local = RotateVector(inverse_rotation, world_vector);
		for (std::size_t component = 0u; component < 3u; ++component) {
			auto const scale = std::abs(parent.scale[component]) < 0.000001f
				? 1.0f
				: parent.scale[component];
			local[component] /= scale;
		}
		return local;
	}

	export std::array<float, 4> SceneViewRotateY(
		std::array<float, 4> const& rotation,
		float const& radians
	) {
		auto const half_angle = radians * 0.5f;
		auto const delta = std::array{ 0.0f, std::sin(half_angle), 0.0f, std::cos(half_angle) };
		return MultiplyQuaternion(delta, rotation);
	}

	export std::array<float, 4> SceneViewLocalRotation(
		SceneViewEntity const& parent,
		std::array<float, 4> const& world_rotation
	) {
		auto const inverse_parent = std::array{
			-parent.rotation[0],
			-parent.rotation[1],
			-parent.rotation[2],
			parent.rotation[3]
		};
		return MultiplyQuaternion(inverse_parent, world_rotation);
	}

	export std::array<float, 3> SceneViewDirection(
		SceneViewEntity const& entity,
		std::array<float, 3> const& local_direction
	) {
		return RotateVector(entity.rotation, local_direction);
	}

	// SubmitSceneView writes the immediate-mode frame mailbox during the application's
	// Tick callback. RenderingSystem::Render consumes it after ImGui::Render on the
	// same thread, so no editor object or pointer crosses the rendering boundary.
	thread_local std::optional<SceneViewSubmission> g_scene_view_submission;

	export void SubmitSceneView(SceneViewSubmission const& submission) {
		g_scene_view_submission = submission;
		ResolveSceneView(*g_scene_view_submission);
	}

	export SceneViewSubmission ConsumeSceneView() {
		if (!g_scene_view_submission) {
			return {};
		}
		auto submission = std::move(*g_scene_view_submission);
		g_scene_view_submission.reset();
		return submission;
	}

}
