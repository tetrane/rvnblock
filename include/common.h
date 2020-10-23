#pragma once

#include <cstdint>

namespace reven {
namespace block {

//! Mode of execution
enum class ExecutionMode : std::uint8_t {
	x86_64_bits = 0,
	x86_32_bits = 1,
	x86_16_bits = 2,
};

//! Helper struct to group a pointer and its size together
//!
//! C++20: std::span
//! lifetime(Span) < lifetime(data)
struct Span {
	//! Size of the data
	std::size_t size;
	//! Pointer to the data
	const std::uint8_t* data;
};

constexpr const char* format_version = "1.0.0";
constexpr const char* writer_version = "1.0.0";

}} // namespace reven::block
