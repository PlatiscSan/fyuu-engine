module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>

#include <string>
#endif // !defined(__cpp_lib_modules)

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :uuid;
import :value;

namespace fyuu_asset {

	Value::Value() noexcept = default;

	Value::Value(Value const& other) = default;

	Value::Value(Value&& other) noexcept = default;

	Value& Value::operator=(Value const& other) = default;

	Value& Value::operator=(Value&& other) noexcept = default;

	Value::~Value() = default;

	Value::Value(bool value) noexcept : m_data(value) {
	}

	Value::Value(std::int64_t value) noexcept : m_data(value) {
	}

	Value::Value(std::uint64_t value) noexcept : m_data(value) {
	}

	Value::Value(double value) noexcept : m_data(value) {
	}

	Value::Value(std::string const& value) : m_data(value) {
	}

	Value::Value(std::string&& value) noexcept : m_data(std::move(value)) {
	}

	Value::Value(Binary const& value) : m_data(value) {
	}

	Value::Value(Binary&& value) noexcept : m_data(std::move(value)) {
	}

	Value::Value(File const& value) : m_data(value) {
	}

	Value::Value(File&& value) noexcept : m_data(std::move(value)) {
	}

	Value::Value(Array const& value) : m_data(value) {
	}

	Value::Value(Array&& value) noexcept : m_data(std::move(value)) {
	}

	Value::Value(Object const& value) : m_data(value) {
	}

	Value::Value(Object&& value) noexcept : m_data(std::move(value)) {
	}

	Value::Value(UUID const& value) noexcept : m_data(value) {
	}

} // namespace fyuu_asset
