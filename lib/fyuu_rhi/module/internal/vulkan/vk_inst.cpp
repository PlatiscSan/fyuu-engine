module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>

#include <sstream>

#include <cstdint>
#include <type_traits>

#include <unordered_set>

#include <string_view>

#include <source_location>
#include <span>

#include <ranges>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <vulkan/vulkan.h>
#if defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#include <android/android_native_app_glue.h>
#endif // defined(_WIN32)
#include "log.hpp"
#endif //!defined(__APPLE__)
module fyuu_rhi:vulkan_instance;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif
import :core_types;
import :log;
import :vulkan_traits;
import :vulkan_utility;
import :cache_system;

namespace {

	using namespace fyuu_rhi;

	vk::Bool32 VKDebugMessager(vk::DebugUtilsMessageSeverityFlagBitsEXT message_severity, vk::DebugUtilsMessageTypeFlagsEXT message_types, vk::DebugUtilsMessengerCallbackDataEXT const* callback_data, void* user_data) {

		// Build a comprehensive message string.
		std::ostringstream message;
		message << "Vulkan ";

		// Append message type(s) as tags.
		if (message_types & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral) {
			message << "[General] ";
		}
		if (message_types & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation) {
			message << "[Validation] ";
		}
		if (message_types & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance) {
			message << "[Performance] ";
		}

		// Append message ID name if available.
		if (callback_data->pMessageIdName) {
			message << std::string(callback_data->pMessageIdName) + ": ";
		}

		// Append the main message text.
		if (callback_data->pMessage) {
			message << callback_data->pMessage;
		}

		// Append information about related Vulkan objects.
		if (callback_data->objectCount > 0) {
			message << "\nRelated Objects:";
			for (std::uint32_t i = 0; i < callback_data->objectCount; ++i) {
				auto const& object = callback_data->pObjects[i];
				message << "\n  - Object " << i <<
					": Type=" << static_cast<std::uint32_t>(object.objectType) <<
					", Handle=" << object.objectHandle;
				if (object.pObjectName) {
					message << ", Name=\"" << object.pObjectName << "\"";
				}
			}
		}

		// Append queue label information.
		if (callback_data->queueLabelCount > 0) {
			message << "\nQueue Labels:";
			for (std::uint32_t i = 0; i < callback_data->queueLabelCount; ++i) {
				auto const& label = callback_data->pQueueLabels[i];
				message << "\n  - Label " << i << ": ";
				if (label.pLabelName) {
					message << label.pLabelName;
				}
			}
		}

		// Append command buffer label information.
		if (callback_data->cmdBufLabelCount > 0) {
			message << "\nCommand Buffer Labels:";
			for (std::uint32_t i = 0; i < callback_data->cmdBufLabelCount; ++i) {
				auto const& label = callback_data->pCmdBufLabels[i];
				message << "\n  - Label " << std::to_string(i) << ": ";
				if (label.pLabelName) {
					message << label.pLabelName;
				}
			}
		}

		if (message_severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError && log::Error) {
			log::Error(message.str(), std::source_location::current());
		}
		else if (message_severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning && log::Warning) {
			log::Warning(message.str(), std::source_location::current());
		}
		else if (log::Info) {
			log::Info(message.str(), std::source_location::current());
		}
		else {

		}

		return vk::False;

	}
}

namespace fyuu_rhi::vulkan {

	Backend::Instance Backend::CreateInstance(
		std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver
#if defined(__ANDROID__)
		, android_app* android_app
#endif // defined(__ANDROID__)
	) {
		
		static vk::detail::DispatchLoaderDynamic dispatcher(vkGetInstanceProcAddr);
		
		std::vector<vk::ExtensionProperties> supported_extensions = vk::enumerateInstanceExtensionProperties(nullptr, dispatcher);
		std::unordered_set<std::string_view> supported_extension_set;
		for (auto const& ext : supported_extensions) {
			supported_extension_set.insert(ext.extensionName);
		}

		vk::ApplicationInfo application_info(
			app_name.data(),
			vk::makeApiVersion(
				app_ver.variant,
				app_ver.major,
				app_ver.minor,
				app_ver.patch
			),
			engine_name.data(),
			vk::makeApiVersion(
				engine_ver.variant,
				engine_ver.major,
				engine_ver.minor,
				engine_ver.patch
			),
			vk::ApiVersion14  // Target Vulkan 1.4
		);

		std::vector<char const*> enabled_extensions;
#if !defined(NDEBUG)
		if (supported_extension_set.count(vk::EXTDebugUtilsExtensionName)) {
			enabled_extensions.emplace_back(vk::EXTDebugUtilsExtensionName);
		}
#endif // !defined(NDEBUG)
		static constexpr char const* required_extensions[] = {
			vk::KHRSurfaceExtensionName,
#if defined(_WIN32)
			vk::KHRWin32SurfaceExtensionName,
#elif defined(__linux__)
			vk::KHRXlibSurfaceExtensionName,
			vk::KHRXcbSurfaceExtensionName,
			vk::KHRWaylandSurfaceExtensionName,
#elif defined(__ANDROID__)
			vk::KHRAndroidSurfaceExtensionName,
#endif
			vk::KHRGetSurfaceCapabilities2ExtensionName,
			vk::EXTSurfaceMaintenance1ExtensionName,
			vk::EXTSwapchainColorSpaceExtensionName,
			vk::KHRGetPhysicalDeviceProperties2ExtensionName
		};

		for (auto ext_name : required_extensions) {
			if (supported_extension_set.count(ext_name)) {
				enabled_extensions.emplace_back(ext_name);
			}
			else {
				LOG_WARNING(std::format("{} is not suppoorted", ext_name));
			}
		}

		std::vector<char const*> enabled_layers;
#if !defined(NDEBUG)
		std::vector<vk::LayerProperties> supported_layers = vk::enumerateInstanceLayerProperties(dispatcher);
		std::unordered_set<std::string> supported_layer_set;
		for (auto const& layer : supported_layers) {
			supported_layer_set.insert(layer.layerName);
		}

		constexpr static char const* const desired_layers[] = {
			"VK_LAYER_KHRONOS_validation",
			"VK_LAYER_LUNARG_gfxreconstruct",
			"VK_LAYER_LUNARG_api_dump",
			"VK_LAYER_LUNARG_crash_diagnostic"
		};
		for (auto layer_name : desired_layers) {
			if (supported_layer_set.count(layer_name)) {
				enabled_layers.emplace_back(layer_name);
			}
		}
#endif // !defined(NDEBUG)
		bool validation_layer_enabled = false;
		for (char const* layer : enabled_layers) {
			if (std::strcmp(layer, "VK_LAYER_KHRONOS_validation") == 0) {
				validation_layer_enabled = true;
				break;
			}
		}

		bool layer_settings_supported = supported_extension_set.count(vk::EXTLayerSettingsExtensionName);

		struct SettingEntry {
			char const* settingName;
			vk::Bool32  value;
		};
		std::array<SettingEntry, 5> setting_entries = {
			{
				{ "lunarg_gpu_assisted", vk::True },
				{ "lunarg_gpu_assisted_reserve_binding_slot", vk::True },
				{ "lunarg_best_practices", vk::True },
				{ "printf", vk::True },
				{ "lunarg_synchronization", vk::True }
			} 
		};

		std::array<vk::LayerSettingEXT, 5> settings_array;
		vk::LayerSettingsCreateInfoEXT layer_settings_info = {};

		bool link_layer_settings = validation_layer_enabled && layer_settings_supported;

		if (link_layer_settings) {

			enabled_extensions.emplace_back(vk::EXTLayerSettingsExtensionName);

			for (size_t i = 0; i < setting_entries.size(); ++i) {
				settings_array[i].pLayerName = "VK_LAYER_KHRONOS_validation";
				settings_array[i].pSettingName = setting_entries[i].settingName;
				settings_array[i].type = vk::LayerSettingTypeEXT::eBool32;
				settings_array[i].valueCount = 1;
				settings_array[i].pValues = &setting_entries[i].value;
			}

			layer_settings_info.sType = vk::StructureType::eLayerSettingsCreateInfoEXT;
			layer_settings_info.settingCount = static_cast<uint32_t>(settings_array.size());
			layer_settings_info.pSettings = settings_array.data();
		}

		vk::InstanceCreateInfo instance_create_info({}, &application_info, enabled_layers, enabled_extensions, link_layer_settings ? &layer_settings_info : nullptr);

		vk::SharedInstance instance(vk::createInstance(instance_create_info, nullptr, dispatcher), { nullptr, dispatcher });

		dispatcher.init(*instance);
#if defined(NDEBUG)
		vk::SharedDebugUtilsMessengerEXT debug_messenger;
#else
		// Capture all severity levels.
		vk::DebugUtilsMessageSeverityFlagsEXT severity_flags(
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
		);

		// Capture all message types.
		vk::DebugUtilsMessageTypeFlagsEXT message_type_flags(
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
			vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
			// eDeviceAddressBinding is omitted (Vulkan 1.2+)
		);

		vk::SharedDebugUtilsMessengerEXT debug_messenger(
			instance->createDebugUtilsMessengerEXT(
				vk::DebugUtilsMessengerCreateInfoEXT{
					{},
					severity_flags,
					message_type_flags,
					VKDebugMessager,
					nullptr	// User data passed to callback
				},
				nullptr,
				dispatcher
			),
			instance,
			{ nullptr, dispatcher }
		);
#endif // defined(NDEBUG)
		cache::Initialize(
			app_name, app_ver, engine_name, engine_ver
#if defined(__ANDROID__)
			, android_app
#endif // defined(__ANDROID__)
		);

		return { dispatcher, ToStrings(enabled_extensions), std::move(instance), std::move(debug_messenger) };

	}

	std::vector<Backend::PhysicalDevice> Backend::EnumeratePhysicalDevices(Backend::Instance const& instance) {
		auto WrapPhysicalDevice = [&instance](vk::PhysicalDevice phys_dev) -> Backend::PhysicalDevice {
			return { instance, vk::SharedPhysicalDevice(phys_dev, instance.impl) };
			};
		return instance.impl->enumeratePhysicalDevices(instance.dispatcher) |
			std::ranges::views::transform(WrapPhysicalDevice) |
			std::ranges::to<std::vector<Backend::PhysicalDevice>>();
	}

}



#endif // !defined(__APPLE__)
