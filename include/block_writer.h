#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <experimental/string_view>

#include "common.h"

#include <rvnsqlite/resource_database.h>

namespace reven {
namespace block {
namespace writer {

//! Indicates which block was executed, as defined by its pc, instruction count and mode
struct ExecutedBlock {
	//! Address of the first instruction executed in the block
	std::uint64_t pc;
	//! Number of instructions in the block
	std::uint16_t block_instruction_count;
	//! Mode in which the block was executed
	ExecutionMode mode;

	bool operator==(const ExecutedBlock& o) const {
		return pc == o.pc and
		        block_instruction_count == o.block_instruction_count and
		        mode == o.mode;
	}

	bool operator!=(const ExecutedBlock& o) const {
		return not (*this == o);
	}
};

//! Write the trace of executed blocks as a versioned database in the format described in
//!   [trace-format.md](../trace-format.md).
class Writer {
public:
	//! Create a new database from the specified filename, tool_name, tool_version and tool_info
	Writer(const char* filename, const char* tool_name, const char* tool_version, const char* tool_info);

	// Rule of five
	~Writer();
	Writer(const Writer&) = delete;
	Writer(Writer&&) = default;
	Writer& operator=(const Writer&) = delete;
	Writer& operator=(Writer&&) = default;

	//! Report the execution of a block to the database
	//!
	//! - current_transition: id of the transition of the first executed instruction of the block
	//! - block: executed block
	//! - instruction_data: data of the executed block
	void add_block(std::uint64_t current_transition, ExecutedBlock block, Span instruction_data);

	//! Report the execution of an instruction at the specified rip in the currently executing block.
	//!
	//! This allows to compute the offsets of each instruction inside the block.
	//!
	//! # Example:
	//!
	//! ```cpp
	//! // add the currently executed block
	//! add_block(current, block, instruction_data);
	//! for (const auto& instruction : executed_block) {
	//! 	// add each instruction of the block
	//! 	add_block_instruction(instruction.pc);
	//! }
	//! ```
	void add_block_instruction(std::uint64_t rip);

	//! Report the execution of a non-instruction to the database
	//!
	//! - current_transition: id of the transition corresponding to the non-instruction
	void add_interrupt(std::uint64_t current_transition);

	//! Indicate that the last basic block finished executing.
	//!
	//! As the final basic block is not necessarily executed fully, call this method to send the
	//! final transition id of the trace
	void finalize_execution(std::uint64_t last_transition_id);

	//! Finalizes any running transaction and recovers the underlying resource database.
	//!
	//! Note that to avoid any leak of resources, the obtained database should not be destroyed
	//! before this instance of Writer is destroyed.
	//!
	//! No method should be called on a moved-out Writer.
	sqlite::ResourceDatabase take() &&;
private:
	static constexpr std::uint32_t TRANSACTION_COUNT = 10000;

	// see boost::uuids::sha1::digest_type
	using Hash = std::vector<unsigned int>;
	using BlockId = std::int64_t;

	// Data of the last block that has been inserted into the database.
	// This is used as compression to generate a single execution event when the same block has been
	// executed several times (such as when the block is looping on itself)
	Hash last_hash_;
	ExecutedBlock last_block_;
	BlockId last_id_ = 0;
	std::vector<uint8_t> last_instruction_data_;
	std::uint64_t last_transition_id_ = 0;
	std::vector<uint32_t> last_block_instruction_indices_;

	// if 0, no transaction is running, otherwise transaction has been running for this number of steps
	std::uint32_t transaction_items_ = 0;

	struct MappedBlock {
		BlockId id;
		std::uint32_t executed_instructions;
		ExecutedBlock block;
	};

	// Boilerplate required to use Hash as key in an unordered_map
	struct Hasher {
		std::size_t operator()(const Hash& hash) const {
			std::experimental::string_view view(reinterpret_cast<const char*>(hash.data()),
			                                    hash.size() * sizeof(unsigned int));
			return std::hash<std::experimental::string_view>()(view);
		}
	};
	struct Equaler {
		bool operator()(const Hash& l, const Hash& r) const {
			return std::equal(l.begin(), l.end(), r.begin(), r.end());
		}
	};
	// Map of known blocks. Used to determine if a new block should be inserted in the database
	std::unordered_map<Hash, MappedBlock, Hasher, Equaler> block_map_;

	reven::sqlite::ResourceDatabase db_;
	reven::sqlite::Statement last_block_stmt_;
	reven::sqlite::Statement instructions_stmt_;
	reven::sqlite::Statement block_execution_stmt_;

	void reset_last_block(ExecutedBlock block, unsigned int* digest, Span instruction_data);
	void insert_last_block();
	std::int64_t insert_block_db(const ExecutedBlock& block, Span instruction_data);
	void insert_executed_instructions_db(const std::vector<std::uint32_t>& block_instruction_indices,
	                                     std::uint32_t already_inserted_instructions);
	void insert_block_execution(std::uint64_t transition_id);
	// use for transaction-aware statement steps
	reven::sqlite::Statement::StepResult step_transaction(reven::sqlite::Statement& stmt);
};

}}} // namespace reven::block::writer
