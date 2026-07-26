#include <iostream>
#include <memory>
#include <utility>
import fyuu_rhi;

namespace {
	struct Backend {
		using Resource = std::unique_ptr<int>;
	};

	bool ResourceIdentityIsStable() {
		fyuu_rhi::ResourceFlags flags;
		flags.Set(fyuu_rhi::ResourceFlagBits::HostVisible);
		fyuu_rhi::Resource<Backend> first(std::make_unique<int>(1), 64u, flags);
		fyuu_rhi::Resource<Backend> second(std::make_unique<int>(2));
		auto first_id = first.ID();
		auto second_id = second.ID();
		if (first_id == 0u || second_id == 0u || first_id == second_id) {
			return false;
		}

		fyuu_rhi::Resource<Backend> moved(std::move(first));
		if (moved.ID() != first_id || first.ID() != 0u ||
			moved.Size() != 64u || first.Size() != 0u ||
			!moved.Flags().Test(fyuu_rhi::ResourceFlagBits::HostVisible)) {
			return false;
		}

		second = std::move(moved);
		return second.ID() == first_id && moved.ID() == 0u;
	}

	bool TextureMetadataSurvivesOwnershipTransfer() {
		fyuu_rhi::ResourceFlags flags;
		flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
		fyuu_rhi::Resource<Backend> texture(
			std::make_unique<int>(1),
			0u,
			flags,
			{ 256u, 128u, 6u, 9u }
		);
		fyuu_rhi::Resource<Backend> moved(std::move(texture));
		auto const& extent = moved.TextureExtent();
		return extent.width == 256u && extent.height == 128u &&
			extent.depth_or_array_layers == 6u && extent.mip_levels == 9u &&
			texture.TextureExtent().width == 0u;
	}
}

int main() {
	if (!ResourceIdentityIsStable()) {
		std::cerr << "resource identity did not survive ownership transfer\n";
		return 1;
	}
	if (!TextureMetadataSurvivesOwnershipTransfer()) {
		std::cerr << "texture metadata did not survive ownership transfer\n";
		return 1;
	}
	return 0;
}
