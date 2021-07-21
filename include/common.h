#pragma once

namespace reven {
namespace block {

//! Mode of execution
enum class ExecutionMode : std::uint8_t {
	x86_64_bits = 0,
	x86_32_bits = 1,
	x86_16_bits = 2,
};


constexpr const char* format_version = "1.0.0";
constexpr const char* writer_version = "1.0.0";

}} // namespace reven::block
