module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <filesystem>
#include <string>
#endif // !defined(__cpp_lib_modules)

export module fyuu_engine:application_types;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_engine {

	export class Runtime;

	export struct ApplicationVersion {
		std::uint8_t variant = 0u;
		std::uint8_t major = 1u;
		std::uint8_t minor = 0u;
		std::uint8_t patch = 0u;
	};

	export struct ApplicationDescriptor {
		std::string description;
		std::string name;
		std::string title;
		std::uint32_t surface_width = 1280u;
		std::uint32_t surface_height = 720u;
		ApplicationVersion version;
		std::filesystem::path configuration_path = "./conf.yaml";
		std::string graphics_api = "PlatformDefault";
		bool vertical_sync = true;
		std::uint32_t frames_in_flight = 3u;
		void* user_data = nullptr;
		void (*Initialize)(Runtime&) = nullptr;
		void (*Tick)(Runtime&) = nullptr;
		void (*Shutdown)(Runtime&) = nullptr;
	};

	export enum class RuntimeState : std::uint8_t {
		Created,
		Initialized,
		Running,
		Stopping,
		Stopped
	};

}
