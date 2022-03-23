#pragma once

#include <cstdint>
#include <random>
#include <algorithm>
#include <string_view>
#include <vector>
#include <initializer_list>
#include <stdexcept>
#include <functional>
#include <iostream>

#define ALWAYS_INLINE __attribute__((always_inline))

namespace detail {

template<int N>
class Generator {
	struct Range {
		int beg = 0, size = 0;
		constexpr Range() = default;
		constexpr Range(int beg, int end) : beg(beg), size(end - beg + 1) {
			if (beg > end) throw std::invalid_argument("Begin of range bigger than end");
		}
	};
	std::array<Range, N> ranges {};
	int totalSize = 0;
public:
	constexpr Generator(std::string_view str) {
		int pos = 0;
		for (size_t i = 0; i < str.length(); i++, pos++) {
			if (i + 2 < str.length() && str[i + 1] == '-') {
				ranges[pos] = { str[i], str[i + 2] };
				i += 2;
			} else
				ranges[pos] = { str[i], str[i] };
			totalSize += ranges[pos].size;
		}
	}
	constexpr char ALWAYS_INLINE operator()(int val) const {
		for (auto [ beg, size ] : ranges) {
			if (val >= size) val -= size;
			else return beg + val;
		}
		throw std::invalid_argument("Out of bounds");
	}
	template<typename T>
	std::string operator()(T& rng, size_t length) const {
		std::uniform_int_distribution<int> distribution(0, totalSize - 1);
		std::string out(length, '\0');
		std::generate(begin(out), end(out), [&]{ return operator()(distribution(rng)); });
		return out;
	}
};

}

template<typename... Lambdas>
constexpr auto passwordGeneratorList(Lambdas... stringHolders) {
	constexpr auto generatorSize = [] (std::string_view str) {
		int n = 0;
		for (size_t i = 0; i < str.length(); i++) {
			if (i + 2 < str.length() && str[i + 1] == '-')
				i += 2;
			n++;
		}
		return n;
	};

	constexpr int generatorMaxSize = std::max({ generatorSize(stringHolders())... });
	return std::array<detail::Generator<generatorMaxSize>, sizeof...(Lambdas)> { stringHolders()... };
}
