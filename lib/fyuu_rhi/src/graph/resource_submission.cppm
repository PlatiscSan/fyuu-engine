module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:resource_submission;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#define FYUU_RHI_RESOURCE_SUBMISSION_STD_INCLUDED
#include "internal/resource_submission.hpp"
#undef FYUU_RHI_RESOURCE_SUBMISSION_STD_INCLUDED
