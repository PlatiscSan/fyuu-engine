module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <string>

#include <cstdint>

#include <optional>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:physical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_device;

export namespace fyuu_rhi {

	class PhysicalDevice {
	public:
		using UniqueHandle = std::unique_ptr<
			struct PhysicalDeviceImplementation,
			void(*)(struct PhysicalDeviceImplementation*)
		>;

	private:
		UniqueHandle m_impl;

	public:
		struct Info {
			std::string name;
			std::optional<std::uint32_t> vendor_id;
			std::optional<std::uint32_t> device_id;
			std::optional<std::size_t> dedicated_memory;
			enum class Type : std::uint8_t {
				Unknown,
				DiscreteGPU, 
				IntegratedGPU, 
				CPU, 
				Virtual
			} type;
		};

		PhysicalDevice() noexcept = default;

		PhysicalDevice(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		Info GetInfo() const;
	
		LogicalDevice CreateLogicalDevice();
	};

	/// Picks the most performant physical device from an enumeration, preferring
	/// discrete GPUs, then integrated GPUs, then type score, breaking ties by
	/// dedicated memory. Returns the winning shared_ptr (never null on success).
	PhysicalDevice const& BestPerformance(std::span<PhysicalDevice const> physical_devices);

} // namespace fyuu_rhi
