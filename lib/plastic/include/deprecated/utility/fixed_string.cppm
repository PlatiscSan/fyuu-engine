module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <algorithm>
#include <iostream>
#endif // !defined(__cpp_lib_modules)
export module plastic.fixed_string;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace plastic::utility {

	export template <std::size_t N> struct FixedString {
		alignas(sizeof(std::size_t)) std::array<char, N> str_impl{};

		FixedString() = default;

		constexpr FixedString(std::array<char, N> const& arr) noexcept
			: str_impl(arr) {

		}

		constexpr FixedString(std::string_view sv) noexcept {
			std::size_t const copy_size = std::min(sv.size(), N - 1);
			std::copy_n(sv.data(), copy_size, str_impl.begin());
			std::fill(str_impl.begin() + copy_size, str_impl.end(), '\0');
		}

		constexpr FixedString(char const(&str)[N]) noexcept {
			std::copy_n(str, N, str_impl.begin());
		}

		constexpr operator std::string_view() const noexcept {
			return { str_impl.data(), N - 1 };
		}

		constexpr operator std::string() const noexcept {
			return { str_impl.data(), N - 1 };
		}

		constexpr auto size() const noexcept {
			return N - 1;
		}

		constexpr char const* c_str() const noexcept {
			return str_impl.data();
		}

		constexpr char operator[](std::size_t i) const noexcept {
			return str_impl[i];
		}

		template <std::size_t M>
		bool operator==(FixedString<M> const& other) const noexcept {
			if constexpr (N != M) {
				return false;
			}
			else {
				return[this, &other] <std::size_t... I> (std::index_sequence<I...>) {
					return ((str_impl[I] == other.str_impl[I]) && ...);
				}(std::make_index_sequence<N>());
			}

		}

	};

	export template <std::size_t N> std::ostream& operator<<(std::ostream& os, FixedString<N> const& fixed_str) {
		return os << fixed_str.c_str();
	}

	export template <std::size_t N1, std::size_t N2> constexpr auto StringCat(
		FixedString<N1> const& s1,
		FixedString<N2> const& s2
	) noexcept {

		std::array<char, N1 + N2 - 1> result{};

		[]<std::size_t... I1>(std::index_sequence<I1...>, auto& result, auto const& s1) {
			((result[I1] = s1[I1]), ...);
		}(std::make_index_sequence<N1 - 1>{}, result, s1);

		[]<std::size_t... I2>(std::index_sequence<I2...>, auto& result, auto const& s2) {
			((result[N1 - 1 + I2] = s2[I2]), ...);
		}(std::make_index_sequence<N2>{}, result, s2);

		return FixedString<N1 + N2 - 1>(result);

	}

}
