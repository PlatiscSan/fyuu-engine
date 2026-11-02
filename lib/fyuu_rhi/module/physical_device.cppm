module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>

#include <cstdint>

#include <optional>

#include <span>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:physical_device;
import :fast_pimpl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_device;

export namespace fyuu_rhi {

	/** @brief Move-only handle describing one adapter exposed by an Instance. */
	class PhysicalDevice {
	public:
		using ImplementationStorage = details::FastPimpl<struct PhysicalDeviceImplementation, 96u>;

	private:
		ImplementationStorage m_impl;

	public:
		/// Backend-independent adapter information; unavailable numeric fields are empty.
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

		PhysicalDevice() noexcept;

		PhysicalDevice(ImplementationStorage&& impl) noexcept;

		PhysicalDevice(PhysicalDevice const&) = delete;
		PhysicalDevice& operator=(PhysicalDevice const&) = delete;
		PhysicalDevice(PhysicalDevice&& other) noexcept;
		PhysicalDevice& operator=(PhysicalDevice&& other) noexcept;
		~PhysicalDevice() noexcept;

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		/// Queries the adapter name, class, identifiers, and dedicated-memory size.
		Info GetInfo() const;
	
		/// Creates a logical device from this adapter.
		LogicalDevice CreateLogicalDevice() const;
	};

	/// Picks the most performant physical device from an enumeration, preferring
	/// discrete GPUs, then integrated GPUs, then type score, breaking ties by
	/// dedicated memory. Returns the winning shared_ptr (never null on success).
	PhysicalDevice const& BestPerformance(std::span<PhysicalDevice const> physical_devices);

} // namespace fyuu_rhi
