module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <utility>
#include <vector>
#include <algorithm>
#include <string>
#include <limits>

#include <cstdint>
#include <array>
#include <unordered_set>

#include <string_view>

#include <source_location>
#include <ranges>
#include <format>
#endif // !defined(__cpp_lib_modules)
#include <vulkan/vulkan.h>
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)

module fyuu_rhi:vulkan_instance;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :instance_dispatch;
import :instance_factory;
import :log;
import :physical_device_factory;
import :vulkan_data;

#endif // !defined(__APPLE__)

#if !defined(__APPLE__)
namespace {

	VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugMessenger(
		vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
		vk::DebugUtilsMessageTypeFlagsEXT types,
		vk::DebugUtilsMessengerCallbackDataEXT const* callback_data,
		void*
	) noexcept {
		try {
			auto message = std::format(
				"Vulkan [{}{}{}] {} ({}): {}",
				types & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral ?
					"General " : "",
				types & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation ?
					"Validation " : "",
				types & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance ?
					"Performance" : "",
				callback_data->pMessageIdName ? callback_data->pMessageIdName : "Unknown",
				callback_data->messageIdNumber,
				callback_data->pMessage ? callback_data->pMessage : ""
			);
			if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
				fyuu_rhi::log::Error(message);
			}
			else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
				fyuu_rhi::log::Warning(message);
			}
			else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
				fyuu_rhi::log::Info(message);
			}
			else {
				fyuu_rhi::log::Debug(message);
			}
		}
		catch (...) {
			fyuu_rhi::log::Error("Failed to format a Vulkan debug message");
		}
		return vk::False;
	}

	bool AddLayer(
		std::unordered_set<std::string_view> const& available_layers,
		std::vector<char const*>& enabled_layers,
		std::string_view layer_name
	) {
		if (!available_layers.contains(layer_name)) {
			fyuu_rhi::log::Warning(
				std::format(
					"Vulkan instance layer '{}' is unavailable",
					layer_name
				)
			);
			return false;
		}
		enabled_layers.emplace_back(layer_name.data());
		return true;
	}

	void AddOptionalExtension(
		std::uint32_t vk_inst_ver,
		std::unordered_set<std::string_view> const& available_extensions,
		std::vector<char const*>& enabled_extensions,
		std::string_view ext_name,
		std::uint32_t supported_vk_inst
	) {
		if (vk_inst_ver >= supported_vk_inst) {
			return;
		}
		if (!available_extensions.contains(ext_name)) {
			fyuu_rhi::log::Warning(
				std::format(
					"Optional Vulkan instance extension '{}' is unavailable",
					ext_name
				)
			);
			return;
		}
		enabled_extensions.emplace_back(ext_name.data());
	}

	void AddMandatoryExtension(std::unordered_set<std::string_view> const& available_extensions, std::vector<char const*>& enabled_extensions) {
		static constexpr std::array common_extensions{
			vk::KHRSurfaceExtensionName
		};
#if defined(_WIN32)
		static constexpr std::array platform_extensions{
			vk::KHRWin32SurfaceExtensionName
		};
#elif defined(__ANDROID__)
		static constexpr std::array platform_extensions{
			vk::KHRAndroidSurfaceExtensionName
		};
#elif defined(__linux__)
		static constexpr std::array platform_extensions{
			vk::KHRXlibSurfaceExtensionName,
			vk::KHRWaylandSurfaceExtensionName
		};
#endif // defined(_WIN32)

		auto ValidateExtension = [&](char const* extension) {
			if (!available_extensions.contains(extension)) {
				throw std::runtime_error(
					std::format(
						"Mandatory Vulkan instance extension '{}' is unavailable",
						extension
					)
				);
			}
		};
		std::ranges::for_each(common_extensions, ValidateExtension);
		std::ranges::for_each(platform_extensions, ValidateExtension);
		enabled_extensions.insert(
			enabled_extensions.end(),
			common_extensions.begin(),
			common_extensions.end()
		);
		enabled_extensions.insert(
			enabled_extensions.end(),
			platform_extensions.begin(),
			platform_extensions.end()
		);
	}

} // namespace
#endif // !defined(__APPLE__)

namespace fyuu_rhi {
	template <>
	struct CreateInstance<vulkan::Instance> {
		vulkan::Instance operator()() const {

			vk::detail::defaultDispatchLoaderDynamic.init(vkGetInstanceProcAddr);
			auto vk_inst_ver = vk::ApiVersion10;
			if (vk::detail::defaultDispatchLoaderDynamic.vkEnumerateInstanceVersion) {
				vk_inst_ver = vk::enumerateInstanceVersion(vk::detail::defaultDispatchLoaderDynamic);
			}

			auto supported_extensions = 
				vk::enumerateInstanceExtensionProperties(nullptr, vk::detail::defaultDispatchLoaderDynamic) |
				std::views::transform(
					[](vk::ExtensionProperties const& prop) -> std::string_view {
						return prop.extensionName;
					}
				) |
				std::ranges::to<std::unordered_set>();

			std::vector<char const*> enabled_extensions;
			std::vector<char const*> enabled_layers;
			bool debug_utils_enabled = false;
			bool validation_layer_enabled = false;
#if !defined(NDEBUG)
			auto supported_layers =
				vk::enumerateInstanceLayerProperties(vk::detail::defaultDispatchLoaderDynamic) |
				std::views::transform(
					[](vk::LayerProperties const& property) -> std::string_view {
						return property.layerName;
					}
				) |
				std::ranges::to<std::unordered_set>();
			validation_layer_enabled = AddLayer(
				supported_layers,
				enabled_layers,
				"VK_LAYER_KHRONOS_validation"
			);
#endif // !defined(NDEBUG)
			AddMandatoryExtension(supported_extensions, enabled_extensions);
			AddOptionalExtension(
				vk_inst_ver,
				supported_extensions,
				enabled_extensions,
				vk::KHRGetPhysicalDeviceProperties2ExtensionName,
				vk::ApiVersion11
			);
			AddOptionalExtension(
				vk_inst_ver,
				supported_extensions,
				enabled_extensions,
				vk::KHRGetSurfaceCapabilities2ExtensionName,
				(std::numeric_limits<std::uint32_t>::max)()
			);
			if (supported_extensions.contains(vk::KHRGetSurfaceCapabilities2ExtensionName)) {
				AddOptionalExtension(
					vk_inst_ver,
					supported_extensions,
					enabled_extensions,
					supported_extensions.contains(vk::KHRSurfaceMaintenance1ExtensionName) ? vk::KHRSurfaceMaintenance1ExtensionName : vk::EXTSurfaceMaintenance1ExtensionName,
					(std::numeric_limits<std::uint32_t>::max)()
				);
			}
			AddOptionalExtension(
				vk_inst_ver,
				supported_extensions,
				enabled_extensions,
				vk::EXTSwapchainColorSpaceExtensionName,
				(std::numeric_limits<std::uint32_t>::max)()
			);
#if !defined(NDEBUG)
			AddOptionalExtension(
				vk_inst_ver,
				supported_extensions,
				enabled_extensions,
				vk::EXTDebugUtilsExtensionName,
				(std::numeric_limits<std::uint32_t>::max)()
			);
			debug_utils_enabled = supported_extensions.contains(vk::EXTDebugUtilsExtensionName);
#endif // !defined(NDEBUG)

			// VK_EXT_layer_settings tunes the Khronos validation layer from the instance
			// pNext chain instead of environment variables. It is only linked when the
			// validation layer is actually loaded (debug builds).
			bool layer_settings_supported =
				supported_extensions.contains(vk::EXTLayerSettingsExtensionName);
			bool link_layer_settings = validation_layer_enabled && layer_settings_supported;
			// These arrays outlive vkCreateInstance; pValues and pSettings point into them.
			std::array setting_values{ vk::True, vk::True, vk::True, vk::True, vk::True };
			std::array setting_names{
				"lunarg_gpu_assisted",
				"lunarg_gpu_assisted_reserve_binding_slot",
				"lunarg_best_practices",
				"printf",
				"lunarg_synchronization"
			};
			std::array<vk::LayerSettingEXT, 5> layer_settings;
			vk::LayerSettingsCreateInfoEXT layer_settings_info;
			if (link_layer_settings) {
				enabled_extensions.emplace_back(vk::EXTLayerSettingsExtensionName);
				std::ranges::transform(
					setting_names,
					setting_values,
					layer_settings.begin(),
					[](char const* name, vk::Bool32 const& value) {
						return vk::LayerSettingEXT(
							"VK_LAYER_KHRONOS_validation",
							name,
							vk::LayerSettingTypeEXT::eBool32,
							1u,
							&value
						);
					}
				);
				layer_settings_info = vk::LayerSettingsCreateInfoEXT(
					static_cast<std::uint32_t>(layer_settings.size()),
					layer_settings.data(),
					nullptr
				);
			}

			auto app_version = ApplicationVersion();
			auto engine_version = EngineVersion();
			vk::ApplicationInfo application_info{
				ApplicationName().data(),
				vk::makeApiVersion(
					app_version.variant,
					app_version.major,
					app_version.minor,
					app_version.patch
				),
				EngineName().data(),
				vk::makeApiVersion(
					engine_version.variant,
					engine_version.major,
					engine_version.minor,
					engine_version.patch
				),
				vk_inst_ver
			};
			vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_info{
				{},
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
					vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
					vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
					vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
				vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
					vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
					vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
				&DebugMessenger
			};

			void const* instance_p_next = nullptr;
			if (debug_utils_enabled) {
				instance_p_next = &debug_messenger_info;
			}
			if (link_layer_settings) {
				layer_settings_info.pNext = instance_p_next;
				instance_p_next = &layer_settings_info;
			}

			vk::InstanceCreateInfo create_info{
				{},
				&application_info,
				enabled_layers,
				enabled_extensions,
				instance_p_next
			};

			vk::SharedInstance instance(
				vk::createInstance(create_info, nullptr, vk::detail::defaultDispatchLoaderDynamic),
				{ nullptr, vk::detail::defaultDispatchLoaderDynamic }
			);

			vk::detail::defaultDispatchLoaderDynamic.init(*instance);
			auto dispatcher = std::make_shared<vk::detail::DispatchLoaderDynamic>(vk::detail::defaultDispatchLoaderDynamic);

			dispatcher->init(*instance);
			vk::SharedDebugUtilsMessengerEXT debug_messenger;
			if (debug_utils_enabled) {
				debug_messenger = vk::SharedDebugUtilsMessengerEXT(
					instance->createDebugUtilsMessengerEXT(debug_messenger_info, nullptr, *dispatcher),
					instance,
					{ nullptr, *dispatcher }
				);
			}
			auto stored_extensions = enabled_extensions |
				std::views::transform(
					[](char const* extension) {
						return std::string_view(extension);
					}
				) |
				std::ranges::to<std::unordered_set>();
			auto stored_layers = enabled_layers |
				std::views::transform(
					[](char const* layer) {
						return std::string_view(layer);
					}
				) |
				std::ranges::to<std::unordered_set>();

			return vulkan::Instance{
				.enabled_extensions = std::move(stored_extensions),
				.enabled_layers = std::move(stored_layers),
				.impl = std::move(instance),
				.dispatcher = std::move(dispatcher),
				.debug_messenger = std::move(debug_messenger)
			};
		}
	};

	template <>
	struct EnumeratePhysicalDevices<vulkan::Instance> {
		vulkan::Instance const* instance;

		std::vector<PhysicalDevice> operator()() const {
			auto native_devices = instance->impl->enumeratePhysicalDevices(*instance->dispatcher);
			return native_devices |
				std::views::transform(
					[this](vk::PhysicalDevice native) {
						return MakePhysicalDevice(
							vulkan::PhysicalDevice{
								vk::SharedPhysicalDevice(native, instance->impl),
								instance->dispatcher,
								instance->enabled_extensions
							}
						);
					}
				) |
				std::ranges::to<std::vector>();
		}
	};
}
