#!/usr/bin/env python3
"""
FyuuEngine preset build script.

Non-interactive build helper. A preset is one complete configuration in the
project's standard schema:
    generator / compiler / platform / build_type / toolchain
The build directory is derived from the naming convention
    {Compiler}_{Generator}[_vcpkg]
so the directory is deterministic and an existing cache is reused when it still
matches the preset (a single-config Ninja directory holds one build type;
switching build type reconfigures the same directory).

Usage:
    python build.py --list
    python build.py --preset <name> [options]
    python build.py --preset clang --target FyuuStudio
    python build.py --preset clang --config Debug
    python build.py --preset msvc --config RelWithDebInfo --target FyuuStudio
    python build.py --preset clang --dry-run
    python build.py --preset clang --no-configure
"""

import argparse
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_ROOT = os.path.join(SCRIPT_DIR, "cmake-build")

DEFAULT_CLANG_DIR = r"C:\Program Files\LLVM"
DEFAULT_VCPKG_ROOT = r"C:\vcpkg"

# ---------------------------------------------------------------------------
# Project configuration schema. These tables match the legacy build script and
# define every value a preset may carry; compilers are names, never paths.
# ---------------------------------------------------------------------------

GENERATOR_MAP = {
	"Ninja": "Ninja",
	"Unix Makefiles": "Unix Makefiles",
	"Xcode": "Xcode",
	"Visual Studio 2026": "Visual Studio 18 2026",
	"Visual Studio 2022": "Visual Studio 17 2022",
	"Visual Studio 2019": "Visual Studio 16 2019",
	"Visual Studio 2017": "Visual Studio 15 2017",
}

# Compiler name -> (C, C++) driver. "{clang}" resolves to the LLVM root.
# MSVC has no explicit drivers: it is driven by the Visual Studio generator.
COMPILER_MAP = {
	"Clang": {"cc": "{clang}/bin/clang.exe", "cxx": "{clang}/bin/clang++.exe"},
	"Clang-cl": {"cc": "{clang}/bin/clang-cl.exe", "cxx": "{clang}/bin/clang-cl.exe"},
	"GCC": {"cc": "gcc", "cxx": "g++"},
	"MSVC": None,
}

PLATFORM_MAP = {
	"x86": "Win32",
	"x64": "x64",
	"ARM64": "ARM64",
	"ARM32": "ARM",
}

BUILD_TYPES = ["Release", "Debug", "RelWithDebInfo", "MinSizeRel"]

TOOLCHAIN_OPTIONS = ["None", "vcpkg"]

# vcpkg triplet suffix per platform.
TRIPLET_SUFFIX = {
	"Windows": "windows",
	"Linux": "linux",
	"Darwin": "osx",
}

# CMake defines applied to every preset unless overridden with --option.
DEFAULT_OPTIONS = {
	"FYUU_BUILD_STUDIO": "ON",
}

# ---------------------------------------------------------------------------
# Presets. Each entry is a complete configuration in the schema above; the
# build directory is derived, not stored, so it always follows the convention.
# ---------------------------------------------------------------------------

PRESETS = {
	"clang": {
		"generator": "Ninja",
		"compiler": "Clang",
		"platform": "x64",
		"build_type": "RelWithDebInfo",
		"toolchain": "vcpkg",
	},
	"clangcl": {
		"generator": "Ninja",
		"compiler": "Clang-cl",
		"platform": "x64",
		"build_type": "RelWithDebInfo",
		"toolchain": "vcpkg",
	},
	"msvc": {
		"generator": "Visual Studio 2026",
		"compiler": "MSVC",
		"platform": "x64",
		"build_type": "Release",
		"toolchain": "vcpkg",
	},
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def safe_dirname(text):
	"""Replace characters that are problematic in directory names."""
	return text.replace(' ', '_').replace('/', '_').replace('\\', '_')


def build_dir_for(config):
	"""Derive the build directory from the {Compiler}_{Generator}[_vcpkg] convention."""
	name = f"{config['compiler']}_{safe_dirname(config['generator'])}"
	if config.get('toolchain') == 'vcpkg':
		name += "_vcpkg"
	return os.path.join(BUILD_ROOT, name)


def default_clang_dir():
	return os.environ.get("FYUU_CLANG_DIR") or DEFAULT_CLANG_DIR


def default_vcpkg_root():
	return os.environ.get("VCPKG_ROOT") or DEFAULT_VCPKG_ROOT


def make_resolver(args):
	"""Return (resolve, clang, vcpkg, triplet) for the current run."""
	clang = args.clang_dir or default_clang_dir()
	vcpkg = args.vcpkg_root or default_vcpkg_root()
	arch = args.arch or "x64"
	if sys.platform == "win32":
		os_name = "Windows"
	elif sys.platform == "darwin":
		os_name = "Darwin"
	else:
		os_name = "Linux"
	suffix = TRIPLET_SUFFIX.get(os_name, "windows")
	triplet = f"{arch}-{suffix}"

	def resolve(text):
		return (text
			.replace("{clang}", clang)
			.replace("{vcpkg}", vcpkg)
			.replace("{triplet}", triplet))

	return resolve, clang, vcpkg, triplet


def is_multi_config(config):
	generator = config["generator"]
	return generator.startswith("Visual Studio") or generator == "Xcode"


def effective_build_type(config, args):
	return args.config or config["build_type"]


def read_cache_value(build_dir, key):
	"""Read `key:TYPE=value` from a CMakeCache.txt; None when absent."""
	cache = os.path.join(build_dir, "CMakeCache.txt")
	if not os.path.isfile(cache):
		return None
	try:
		with open(cache, "r", encoding="utf-8", errors="replace") as file:
			for line in file:
				line = line.rstrip("\r\n")
				if "=" not in line:
					continue
				name, _, value = line.partition("=")
				if ":" in name:
					name, _ = name.split(":", 1)
				if name == key:
					return value
	except OSError:
		return None
	return None


def same_path(left, right):
	return os.path.normcase(os.path.normpath(left)) == os.path.normcase(os.path.normpath(right))


def cache_matches(config, args, resolve, build_dir):
	"""True when the existing cache was configured with the same key settings."""
	if read_cache_value(build_dir, "CMAKE_GENERATOR") != GENERATOR_MAP[config["generator"]]:
		return False
	compiler = COMPILER_MAP[config["compiler"]]
	if compiler:
		cached_cxx = read_cache_value(build_dir, "CMAKE_CXX_COMPILER")
		if cached_cxx is None or not same_path(cached_cxx, resolve(compiler["cxx"])):
			return False
	if config.get("toolchain") == "vcpkg":
		cached_toolchain = read_cache_value(build_dir, "CMAKE_TOOLCHAIN_FILE")
		expected = resolve("{vcpkg}/scripts/buildsystems/vcpkg.cmake")
		if cached_toolchain is None or not same_path(cached_toolchain, expected):
			return False
	return True


def configure_args(config, args, resolve, build_dir):
	"""Build the `cmake -B` command for a preset plus CLI overrides."""
	cmd = ["cmake", "-B", build_dir, "-G", GENERATOR_MAP[config["generator"]]]
	if config["generator"].startswith("Visual Studio"):
		cmd += ["-A", PLATFORM_MAP.get(config["platform"], config["platform"])]
	elif config["generator"] == "Xcode" and sys.platform == "darwin":
		xcode_arch = {"x64": "x86_64", "x86": "i386", "ARM64": "arm64"}.get(config["platform"])
		if xcode_arch:
			cmd += [f"-DCMAKE_OSX_ARCHITECTURES={xcode_arch}"]

	compiler = COMPILER_MAP[config["compiler"]]
	if compiler:
		cc = resolve(compiler["cc"])
		cxx = resolve(compiler["cxx"])
		for compiler_path in (cc, cxx):
			if not os.path.isfile(compiler_path):
				print(f"Warning: compiler not found: {compiler_path}")
		cmd += [f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}"]

	if config.get("toolchain") == "vcpkg":
		toolchain_file = resolve("{vcpkg}/scripts/buildsystems/vcpkg.cmake")
		if not os.path.isfile(toolchain_file):
			print(f"Warning: vcpkg toolchain file not found: {toolchain_file}")
		cmd += [
			f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
			f"-DVCPKG_TARGET_TRIPLET={resolve('{triplet}')}",
		]

	cmd.append(f"-DCMAKE_BUILD_TYPE={effective_build_type(config, args)}")

	options = dict(DEFAULT_OPTIONS)
	for item in args.option:
		name, _, value = item.partition("=")
		if not name:
			print(f"Warning: ignoring malformed option '{item}'")
			continue
		options[name] = value
	for name, value in options.items():
		cmd.append(f"-D{name}={value}")

	return cmd


def build_args(config, args, build_dir):
	"""Build the `cmake --build` command; multi-config generators carry --config."""
	cmd = ["cmake", "--build", build_dir]
	if is_multi_config(config):
		cmd += ["--config", effective_build_type(config, args)]
	for target in args.target:
		cmd += ["--target", target]
	if args.jobs:
		cmd += ["--parallel", str(args.jobs)]
	return cmd


def run(cmd, dry_run):
	print("+ " + " ".join(cmd))
	if dry_run:
		return 0
	return subprocess.run(cmd, cwd=SCRIPT_DIR).returncode


def run_preset(config, args, resolve):
	build_dir = build_dir_for(config)
	os.makedirs(build_dir, exist_ok=True)

	if args.codegen_check:
		check = [sys.executable,
			os.path.join(SCRIPT_DIR, "script", "generate_runtime_api.py"), "--check"]
		if run(check, args.dry_run) != 0:
			print("Codegen check failed; aborting before configure.")
			return 1

	# Decide whether a fresh configure is required.
	need_configure = True
	cached_build_type = read_cache_value(build_dir, "CMAKE_BUILD_TYPE")
	if not args.force_configure and cache_matches(config, args, resolve, build_dir):
		if is_multi_config(config) or cached_build_type == effective_build_type(config, args):
			need_configure = False

	if args.no_configure and need_configure and not args.force_configure:
		if read_cache_value(build_dir, "CMAKE_GENERATOR") is None:
			print("--no-configure requested but no cache exists; configuring.")
		elif not is_multi_config(config) and cached_build_type != effective_build_type(config, args):
			print("--no-configure requested but the build type changed; configuring.")
		else:
			need_configure = False

	if need_configure:
		print("=" * 60)
		print(f"Build directory: {build_dir}")
		print("Configuring...")
		if run(configure_args(config, args, resolve, build_dir), args.dry_run) != 0:
			print("CMake configuration failed.")
			return 1

	print("=" * 60)
	print(f"Build directory: {build_dir}")
	print("Building...")
	return run(build_args(config, args, build_dir), args.dry_run)


def print_presets():
	print("Available presets (directory derived from {Compiler}_{Generator}[_vcpkg]):")
	print(f"  {'name':<10} {'directory':<32} {'generator':<20} {'compiler':<12} {'type':<14} {'toolchain'}")
	for name, config in PRESETS.items():
		print(f"  {name:<10} {os.path.basename(build_dir_for(config)):<32} "
			f"{config['generator']:<20} {config['compiler']:<12} {config['build_type']:<14} {config['toolchain']}")


def parse_args():
	parser = argparse.ArgumentParser(
		description="FyuuEngine preset build script (non-interactive).",
		formatter_class=argparse.RawDescriptionHelpFormatter,
		epilog="Presets:\n" + "\n".join(
			f"  {name:<10} {os.path.basename(build_dir_for(config))}"
			for name, config in PRESETS.items()),
	)
	parser.add_argument("--list", action="store_true",
		help="list available presets and exit")
	parser.add_argument("--preset", choices=sorted(PRESETS),
		help="build configuration to use")
	parser.add_argument("--target", action="append", default=[],
		help="target to build (repeatable, e.g. --target FyuuStudio)")
	parser.add_argument("--config", choices=BUILD_TYPES,
		help="build type override; on single-config Ninja this reconfigures the directory")
	parser.add_argument("--option", action="append", default=[],
		help="extra CMake define NAME=VALUE (repeatable)")
	parser.add_argument("--jobs", type=int, help="parallel build jobs")
	parser.add_argument("--no-configure", action="store_true",
		help="build only; fail-safe configure only when the cache is missing or stale")
	parser.add_argument("--force-configure", action="store_true",
		help="always reconfigure even when the existing cache matches")
	parser.add_argument("--dry-run", action="store_true",
		help="print commands without running them")
	parser.add_argument("--codegen-check", action="store_true",
		help="run script/generate_runtime_api.py --check before configuring")
	parser.add_argument("--clang-dir", help="LLVM installation root (default %(default)s)")
	parser.add_argument("--vcpkg", dest="vcpkg_root", help="vcpkg installation root (default %(default)s)")
	parser.add_argument("--arch", default="x64", help="target architecture for the vcpkg triplet (default %(default)s)")
	return parser.parse_args()


def main():
	args = parse_args()
	if args.list or not args.preset:
		print_presets()
		if not args.preset:
			print("\nUse --preset <name> to build.")
		return 0

	config = PRESETS[args.preset]
	resolve, _, _, _ = make_resolver(args)
	print("=" * 60)
	print(f"Preset:      {args.preset}")
	print(f"Generator:   {config['generator']}")
	print(f"Build type:  {effective_build_type(config, args)}")
	compiler = COMPILER_MAP[config["compiler"]]
	if compiler:
		print(f"C++ compiler: {resolve(compiler['cxx'])}")
	print(f"Directory:   {build_dir_for(config)}")
	print("=" * 60)

	return run_preset(config, args, resolve)


if __name__ == "__main__":
	sys.exit(main())
