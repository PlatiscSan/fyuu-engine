module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <vector>

#include <algorithm>
#include <functional>

#include <cstring>
#include <string>

#include <limits>

#include <cstdint>

#include <array>

#include <random>

#include <optional>

#include <string_view>

#include <compare>
#endif // !defined(__cpp_lib_modules)

module fyuu_studio:model;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_studio::model {

	// Minimal 3D math. The viewport renderer and gizmo pipeline will grow these
	// types; keep them dependency-free so the model stays buildable without FyuuRHI.

	/// 3-component vector with editor defaults (zero).
	struct Float3 {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	/// Unit quaternion (x, y, z, w); the default is the identity rotation.
	struct Quat {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;

		[[nodiscard]] static Quat Identity() noexcept {
			return {};
		}

		friend Quat operator*(Quat const& left, Quat const& right) noexcept {
			return {
				left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
				left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
				left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
				left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z
			};
		}
	};

	/// 4x4 column-major matrix; column `c` occupies elements `c*4` through `c*4+3`.
	struct Matrix4 {
		std::array<float, 16> elements{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		[[nodiscard]] static Matrix4 Translation(Float3 translation) noexcept;
		[[nodiscard]] static Matrix4 Rotation(Quat rotation) noexcept;
		[[nodiscard]] static Matrix4 Scale(Float3 scale) noexcept;

		friend Matrix4 operator*(Matrix4 const& left, Matrix4 const& right) noexcept;
	};

	Matrix4 Matrix4::Translation(Float3 translation) noexcept {
		Matrix4 result;
		result.elements[12] = translation.x;
		result.elements[13] = translation.y;
		result.elements[14] = translation.z;
		return result;
	}

	Matrix4 Matrix4::Scale(Float3 scale) noexcept {
		Matrix4 result;
		result.elements[0] = scale.x;
		result.elements[5] = scale.y;
		result.elements[10] = scale.z;
		return result;
	}

	Matrix4 Matrix4::Rotation(Quat rotation) noexcept {
		auto const xx = rotation.x * rotation.x;
		auto const yy = rotation.y * rotation.y;
		auto const zz = rotation.z * rotation.z;
		auto const xy = rotation.x * rotation.y;
		auto const xz = rotation.x * rotation.z;
		auto const yz = rotation.y * rotation.z;
		auto const wx = rotation.w * rotation.x;
		auto const wy = rotation.w * rotation.y;
		auto const wz = rotation.w * rotation.z;
		Matrix4 result;
		result.elements[0] = 1.0f - 2.0f * (yy + zz);
		result.elements[1] = 2.0f * (xy + wz);
		result.elements[2] = 2.0f * (xz - wy);
		result.elements[4] = 2.0f * (xy - wz);
		result.elements[5] = 1.0f - 2.0f * (xx + zz);
		result.elements[6] = 2.0f * (yz + wx);
		result.elements[8] = 2.0f * (xz + wy);
		result.elements[9] = 2.0f * (yz - wx);
		result.elements[10] = 1.0f - 2.0f * (xx + yy);
		return result;
	}

	Matrix4 operator*(Matrix4 const& left, Matrix4 const& right) noexcept {
		Matrix4 result;
		for (std::size_t column = 0u; column < 4u; ++column) {
			for (std::size_t row = 0u; row < 4u; ++row) {
				auto sum = 0.0f;
				for (std::size_t k = 0u; k < 4u; ++k) {
					sum += left.elements[k * 4u + row] * right.elements[column * 4u + k];
				}
				result.elements[column * 4u + row] = sum;
			}
		}
		return result;
	}

	/// Local-to-parent transform. ToMatrix composes scale, then rotation, then translation.
	struct Transform {
		Float3 position;
		Quat rotation;
		Float3 scale{ 1.0f, 1.0f, 1.0f };

		[[nodiscard]] Matrix4 ToMatrix() const noexcept {
			return Matrix4::Translation(position) *
				Matrix4::Rotation(rotation) *
				Matrix4::Scale(scale);
		}
	};

	/// 128-bit identity stable across save and load.
	///
	/// Stand-in for `fyuu_asset::UUID`: Studio does not link FyuuAsset yet, so this
	/// type is self-contained. It exposes the same operations the persistence bridge
	/// needs (Generate, Parse, ToString, IsNil, ordering, hash); replace the type and
	/// the `#include` with `fyuu_asset::UUID` once Studio links FyuuAsset.
	class PersistentID {
	private:
		friend struct std::hash<PersistentID>;
		std::array<std::uint8_t, 16u> m_bytes{};

		static std::uint8_t HexDigit(char c) {
			if (c >= '0' && c <= '9') {
				return static_cast<std::uint8_t>(c - '0');
			}
			if (c >= 'a' && c <= 'f') {
				return static_cast<std::uint8_t>(c - 'a' + 10u);
			}
			if (c >= 'A' && c <= 'F') {
				return static_cast<std::uint8_t>(c - 'A' + 10u);
			}
			throw std::invalid_argument("PersistentID contains a non-hexadecimal character");
		}

	public:
		PersistentID() noexcept = default;

		[[nodiscard]] static PersistentID Generate() {
			static std::mt19937_64 generator{ std::random_device{}() };
			PersistentID result;
			auto const first = generator();
			auto const second = generator();
			std::memcpy(result.m_bytes.data(), &first, sizeof(first));
			std::memcpy(result.m_bytes.data() + sizeof(first), &second, sizeof(second));
			return result;
		}

		[[nodiscard]] static PersistentID Parse(std::string_view hex) {
			if (hex.size() != 32u) {
				throw std::invalid_argument("PersistentID must be 32 hexadecimal characters");
			}
			PersistentID result;
			for (std::size_t i = 0u; i < 16u; ++i) {
				result.m_bytes[i] = static_cast<std::uint8_t>(
					(HexDigit(hex[i * 2u]) << 4u) | HexDigit(hex[i * 2u + 1u])
				);
			}
			return result;
		}

		[[nodiscard]] bool IsNil() const noexcept {
			for (auto const byte : m_bytes) {
				if (byte != 0u) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] std::string ToString() const {
			static constexpr char Hex[] = "0123456789abcdef";
			std::string result;
			result.reserve(32u);
			for (auto const byte : m_bytes) {
				result.push_back(Hex[byte >> 4u]);
				result.push_back(Hex[byte & 0x0Fu]);
			}
			return result;
		}

		[[nodiscard]] std::strong_ordering operator<=>(PersistentID const& other) const noexcept {
			for (std::size_t i = 0u; i < m_bytes.size(); ++i) {
				if (auto const ordering = m_bytes[i] <=> other.m_bytes[i];
					ordering != std::strong_ordering::equal) {
					return ordering;
				}
			}
			return std::strong_ordering::equal;
		}

		[[nodiscard]] bool operator==(PersistentID const& other) const noexcept {
			return std::is_eq(*this <=> other);
		}
	};

	/// Attached camera state; present on an entity that should be rendered as a camera.
	struct CameraComponent {
		float vertical_fov_degrees = 60.0f;
		float near_plane = 0.1f;
		float far_plane = 1000.0f;
	};

	/// Attached light state; the viewport uses kind, color, and intensity for shading.
	struct LightComponent {
		enum class Kind {
			Directional,
			Point,
			Spot
		};

		Kind kind = Kind::Directional;
		Float3 color{ 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
	};

	/// The editable payload of one scene node. Structural state (parent, children,
	/// sibling order, liveness) lives in SceneGraph slots, not here.
	struct Entity {
		PersistentID persistent_id;
		std::string name;
		bool visible = true;
		Transform local;

		// Component slots; empty means the entity does not carry the component.
		std::optional<CameraComponent> camera;
		std::optional<LightComponent> light;

		// References to fyuu_asset assets by persistent identity; nil when unset.
		PersistentID mesh;
		PersistentID material;
	};

	/// Stable runtime handle. Indices are never reused within a session and deleted
	/// slots become tombstones, so a handle stays meaningful for command undo/redo
	/// and for UI nodes that hold it across frames.
	class EntityID {
	private:
		static constexpr std::size_t Invalid = (std::numeric_limits<std::size_t>::max)();
		std::size_t m_index = Invalid;

	public:
		EntityID() noexcept = default;
		explicit constexpr EntityID(std::size_t index) noexcept
			: m_index(index) {
		}

		[[nodiscard]] std::size_t Index() const noexcept {
			return m_index;
		}

		[[nodiscard]] bool Valid() const noexcept {
			return m_index != Invalid;
		}

		[[nodiscard]] friend bool operator==(EntityID left, EntityID right) noexcept = default;
		[[nodiscard]] friend auto operator<=>(EntityID left, EntityID right) noexcept = default;
	};

	/// Coarse scene-change notification. The Studio UI rebuilds its visual tree each
	/// frame, so a single dirty callback is sufficient today; split into granular
	/// events when a consumer needs to skip full rebuilds.
	class ChangeSink {
	public:
		virtual ~ChangeSink() noexcept = default;
		virtual void OnSceneChanged() = 0;
	};

	/// Owns the entity arena and all structural operations.
	///
	/// Every mutation is immediately reflected and notifies subscribed sinks.
	/// Deleting marks slots dead instead of erasing them, which makes undo trivial:
	/// Reactivate restores the exact same handles and links.
	class SceneGraph {
	private:
		struct Slot {
			Entity entity;
			EntityID parent;
			std::vector<EntityID> children; // insertion-ordered; may contain tombstones
			bool alive = false;
		};

		std::vector<Slot> m_slots;
		std::vector<ChangeSink*> m_sinks; // borrowed, non-owning

		[[nodiscard]] Slot& Require(EntityID id) {
			auto& slot = m_slots.at(id.Index());
			if (!slot.alive) {
				throw std::out_of_range("SceneGraph entity is not alive");
			}
			return slot;
		}

		[[nodiscard]] Slot const& Require(EntityID id) const {
			auto const& slot = m_slots.at(id.Index());
			if (!slot.alive) {
				throw std::out_of_range("SceneGraph entity is not alive");
			}
			return slot;
		}

		[[nodiscard]] bool IsDescendant(EntityID ancestor, EntityID candidate) const noexcept {
			if (ancestor == candidate) {
				return true;
			}
			if (!Alive(ancestor)) {
				return false;
			}
			auto current = candidate;
			for (std::size_t depth = 0u; current.Valid(); ++depth) {
				if (depth > m_slots.size()) {
					return false; // defensive cycle guard
				}
				auto const& slot = m_slots[current.Index()];
				if (!slot.alive) {
					return false;
				}
				if (slot.parent == ancestor) {
					return true;
				}
				current = slot.parent;
			}
			return false;
		}

		void NotifyChange() noexcept {
			for (auto* sink : m_sinks) {
				sink->OnSceneChanged();
			}
		}

	public:
		SceneGraph() = default;
		~SceneGraph() noexcept = default;
		SceneGraph(SceneGraph const&) = default;
		SceneGraph& operator=(SceneGraph const&) = default;
		SceneGraph(SceneGraph&&) noexcept = default;
		SceneGraph& operator=(SceneGraph&&) noexcept = default;

		// -- structural operations ------------------------------------------------

		/// Appends a new entity under `parent` (nil parent means root) and returns its handle.
		EntityID Create(std::string name, EntityID parent) {
			if (parent.Valid() && !Alive(parent)) {
				throw std::invalid_argument("SceneGraph::Create parent is not alive");
			}
			EntityID const id{ m_slots.size() };
			auto& slot = m_slots.emplace_back();
			slot.entity.name = std::move(name);
			slot.entity.persistent_id = PersistentID::Generate();
			slot.parent = parent;
			slot.alive = true;
			if (parent.Valid()) {
				m_slots[parent.Index()].children.push_back(id);
			}
			NotifyChange();
			return id;
		}

		/// Marks `id` and every descendant dead. Links are preserved for undo.
		bool Remove(EntityID id) {
			if (!Alive(id)) {
				return false;
			}
			for (auto const entry : CollectSubtree(id)) {
				m_slots[entry.Index()].alive = false;
			}
			NotifyChange();
			return true;
		}

		/// Destroys every slot, including tombstones. Not undoable; used by NewScene.
		void Clear() {
			if (m_slots.empty()) {
				return;
			}
			m_slots.clear();
			NotifyChange();
		}

		/// Undo/redo primitive: flips one slot's liveness without touching links.
		/// Do not call for ordinary editing; use the command-based EditSession.
		bool SetAlive(EntityID id, bool alive) {
			if (!id.Valid() || id.Index() >= m_slots.size()) {
				return false;
			}
			auto& slot = m_slots[id.Index()];
			if (slot.alive == alive) {
				return false;
			}
			slot.alive = alive;
			NotifyChange();
			return true;
		}

		bool Deactivate(EntityID id) {
			return SetAlive(id, false);
		}

		bool Reactivate(EntityID id) {
			return SetAlive(id, true);
		}

		/// Moves `id` to the end of `new_parent`'s children list. Rejects cycles.
		bool Reparent(EntityID id, EntityID new_parent) {
			if (!CanReparent(id, new_parent)) {
				return false;
			}
			auto& slot = m_slots[id.Index()];
			if (slot.parent == new_parent) {
				return false;
			}
			if (slot.parent.Valid()) {
				std::erase(m_slots[slot.parent.Index()].children, id);
			}
			slot.parent = new_parent;
			if (new_parent.Valid()) {
				m_slots[new_parent.Index()].children.push_back(id);
			}
			NotifyChange();
			return true;
		}

		[[nodiscard]] bool CanReparent(EntityID id, EntityID new_parent) const noexcept {
			if (!Alive(id)) {
				return false;
			}
			if (id == new_parent) {
				return false;
			}
			if (new_parent.Valid() && !Alive(new_parent)) {
				return false;
			}
			if (new_parent.Valid() && IsDescendant(id, new_parent)) {
				return false;
			}
			return true;
		}

		// -- value mutations ------------------------------------------------------

		void Rename(EntityID id, std::string name) {
			Require(id).entity.name = std::move(name);
			NotifyChange();
		}

		void SetVisible(EntityID id, bool visible) {
			Require(id).entity.visible = visible;
			NotifyChange();
		}

		void SetLocalTransform(EntityID id, Transform transform) {
			Require(id).entity.local = std::move(transform);
			NotifyChange();
		}

		// -- queries --------------------------------------------------------------

		[[nodiscard]] Entity& Get(EntityID id) {
			return Require(id).entity;
		}

		[[nodiscard]] Entity const& Get(EntityID id) const {
			return Require(id).entity;
		}

		[[nodiscard]] bool Alive(EntityID id) const noexcept {
			if (!id.Valid() || id.Index() >= m_slots.size()) {
				return false;
			}
			return m_slots[id.Index()].alive;
		}

		[[nodiscard]] EntityID Parent(EntityID id) const {
			return Alive(id) ? Require(id).parent : EntityID{};
		}

		/// Live children of `id` in sibling order; empty when `id` is invalid.
		[[nodiscard]] std::vector<EntityID> Children(EntityID id) const {
			std::vector<EntityID> result;
			if (!Alive(id)) {
				return result;
			}
			for (auto const child : Require(id).children) {
				if (m_slots[child.Index()].alive) {
					result.push_back(child);
				}
			}
			return result;
		}

		/// Top-level entities (nil parent) in slot order.
		[[nodiscard]] std::vector<EntityID> Roots() const {
			std::vector<EntityID> result;
			for (std::size_t i = 0u; i < m_slots.size(); ++i) {
				auto const& slot = m_slots[i];
				if (slot.alive && !slot.parent.Valid()) {
					result.emplace_back(i);
				}
			}
			return result;
		}

		/// `id` and every live descendant, breadth-first. Used for cascade delete and
		/// for collecting the set a RemoveEntityCommand must restore.
		[[nodiscard]] std::vector<EntityID> CollectSubtree(EntityID id) const {
			std::vector<EntityID> result;
			if (!Alive(id)) {
				return result;
			}
			result.push_back(id);
			for (std::size_t index = 0u; index < result.size(); ++index) {
				for (auto const child : Require(result[index]).children) {
					if (m_slots[child.Index()].alive) {
						result.push_back(child);
					}
				}
			}
			return result;
		}

		[[nodiscard]] EntityID FindByPersistentID(PersistentID const& persistent_id) const {
			for (std::size_t i = 0u; i < m_slots.size(); ++i) {
				if (m_slots[i].alive && m_slots[i].entity.persistent_id == persistent_id) {
					return EntityID{ i };
				}
			}
			return {};
		}

		/// Number of live entities (tombstones excluded).
		[[nodiscard]] std::size_t Size() const noexcept {
			std::size_t count = 0u;
			for (auto const& slot : m_slots) {
				if (slot.alive) {
					++count;
				}
			}
			return count;
		}

		[[nodiscard]] bool Empty() const noexcept {
			return Size() == 0u;
		}

		/// Composes the entity's local transform with every ancestor's local transform.
		[[nodiscard]] Matrix4 WorldTransform(EntityID id) const {
			Matrix4 world = Require(id).entity.local.ToMatrix();
			auto current = id;
			for (std::size_t depth = 0u; current.Valid() && depth <= m_slots.size(); ++depth) {
				auto const parent = Require(current).parent;
				if (!parent.Valid()) {
					break;
				}
				world = Require(parent).entity.local.ToMatrix() * world;
				current = parent;
			}
			return world;
		}

		// -- change notification --------------------------------------------------

		void Subscribe(ChangeSink& sink) {
			m_sinks.push_back(&sink);
		}

		void Unsubscribe(ChangeSink& sink) {
			std::erase(m_sinks, &sink);
		}
	};

}

template <> struct std::hash<fyuu_studio::model::PersistentID> {
	[[nodiscard]] std::size_t operator()(fyuu_studio::model::PersistentID const& value) const noexcept {
		std::size_t hash = 0u;
		for (auto const byte : value.m_bytes) {
			hash = hash * 131u + static_cast<std::size_t>(byte);
		}
		return hash;
	}
};
