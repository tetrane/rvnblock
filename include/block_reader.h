#pragma once

#include <cstdint>
#include <experimental/optional>
#include <unordered_map>
#include <vector>

#include "common.h"

#include <rvnsqlite/resource_database.h>

namespace reven {
namespace block {
namespace reader {

//! A block of instructions as stored in the database
struct InstructionBlock {
	//! Data of the instructions executed in this block
	std::vector<std::uint8_t> instruction_data;
	//! Address of the first instruction executed in the block
	std::uint64_t first_pc;
	//! Number of instructions in the block
	std::uint16_t instruction_count;
	//! Mode in which the block was executed
	ExecutionMode mode;
};


//! The data and pc of an instruction.
struct Instruction {
	std::uint64_t pc;
	Span data;
};

//! A block of instruction along with the indexes of its executed instructions.
//!
//! Provides methods to access the individual instructions of the block.
class BlockInstructions {
public:
	BlockInstructions(const InstructionBlock& block, std::vector<std::uint32_t> instruction_indexes) :
	    block_(&block),
	    instruction_indexes_(std::move(instruction_indexes))
	{}

	//! The underlying block
	const InstructionBlock& block() const { return *block_; }

	//! Get the indexth instruction from the block, or nullopt if the index is greater than or equal to the
	//! instruction count.
	std::experimental::optional<Instruction> instruction(std::uint32_t instruction_index) const {
		if (instruction_index >= instruction_count()) {
			return {};
		}
		std::uint32_t begin = 0;
		if (instruction_index != 0) {
			begin = instruction_indexes_[instruction_index - 1];
		}
		std::uint32_t end = block().instruction_data.size();
		if (instruction_index < instruction_indexes_.size()) {
			end = instruction_indexes_[instruction_index];
		}
		std::size_t size = end - begin;
		return Instruction{block().first_pc + begin, {size, block().instruction_data.data() + begin}};
	}

	//! The number of instructions executed at least once in this block.
	//!
	//! This can be different from the InstructionBlock::instruction_count field if the block was never fully executed.
	std::uint32_t instruction_count() const {
		return block().instruction_count == 0 ? 0 : instruction_indexes_.size() + 1;
	}

	//! Performance helping method that allows to shred this BlockInstructions to recover its underlying vector.
	//!
	//! This allows reusing vectors of BlockInstructions rather than allocating new ones for each new instance.
	std::vector<std::uint32_t> take_instruction_indexes() && {
		return std::move(this->instruction_indexes_);
	}
private:
	const InstructionBlock* block_;
	std::vector<std::uint32_t> instruction_indexes_;
};

//! An opaque handle to a block of instructions in the database
class BlockHandle {
public:
	//! Retrieve the numeric value of the handle (rowid of the block in the database)
	//!
	//! For debugging purposes.
	std::int32_t handle() const {
		return handle_;
	}

	//! Block handle of a special block corresponding to a range made of a single non-instruction.
	//!
	//! The pc and mode of the corresponding block should not be accessed, its instruction_count is always 0.
	static BlockHandle interrupt_block_handle() {
		return BlockHandle{1};
	}

	bool operator==(const BlockHandle& o) const {
		return handle_ == o.handle_;
	}

	bool operator!=(const BlockHandle& o) const {
		return handle_ != o.handle_;
	}
private:
	std::int32_t handle_;

	BlockHandle(std::int32_t handle) : handle_(handle) {}
	friend class Reader;
	friend struct EventQueryState;
};

//! An event representing a range a transitions where a block was executed.
struct BlockExecutionEvent {
	//! Id of the first transition executed in the referenced block.
	std::uint64_t begin_transition_id;
	//! Id of the first transition executed **after** the referenced block.
	std::uint64_t end_transition_id;
	//! Handle to the executed block.
	BlockHandle block_handle;

	//! Number of executed transitions in this block.
	//!
	//! Several possibilities here:
	//!   1. execution_count() <= block_instruction_count: the execution of the block was interrupted by a fault
	//!   2. execution_count() == block_instruction_count: the execution of the block completed
	//!   3. execution_count() > block_instruction_count: has_instructions() == false
	std::uint64_t execution_count() const {
		return end_transition_id - begin_transition_id;
	}

	//! Whether the range of executed transitions contains instructions or not
	bool has_instructions() const {
		return block_handle != BlockHandle::interrupt_block_handle();
	}
};

//! Read a file in the format described in [trace-format.md](../trace-format.md) as the trace of executed blocks.
class Reader {
public:
	using EventQuery = sqlite::Query<BlockExecutionEvent, std::function<BlockExecutionEvent(sqlite::Statement&)>>;

	using TransitionQuery = sqlite::Query<std::uint64_t, std::function<std::uint64_t(sqlite::Statement&)>>;

	//! Attempt to open the file specified by filename
	//!
	//! Throws RuntimeError if the file cannot be opened, is not in the correct format or not in the correct version
	Reader(const char* filename);

	//! Attempt to open the resource database passed as parameter
	//!
	//! Throws RuntimeError if the database is not in the correct format or not in the correct version
	Reader(sqlite::ResourceDatabase db);

	//! Attempt to retrieve a block of instructions from its handle.
	//!
	//! The handle can be obtained from the BlockExecutionEvent returned by event_at and query_events.
	//!
	//! The reader uses a block cache, so requesting twice the same block will not read from the database
	//!
	//! Throws RuntimeError if the block corresponding to the handle is not in the database.
	//!        This can happen if a handle obtained from a different BlockReader is passed to this function.
	const InstructionBlock& block(BlockHandle handle) const;

	//! Attempt to retrieve a block of instructions with the indexes of instructions from its handle.
	//!
	//! The handle can be obtained from the BlockExecutionEvent returned by event_at and query_events.
	//!
	//! The reader uses a block cache, so requesting twice the same block will not read from the database
	//!
	//! The instruction_indexes parameter is an arbitrary vector whose backing storage will be reused in the constructed
	//! BlockInstructions. This spares an allocation if the vector already has enough capacity.
	//!
	//! Throws RuntimeError if the block corresponding to the handle is not in the database.
	//!        This can happen if a handle obtained from a different BlockReader is passed to this function.
	BlockInstructions block_with_instructions(BlockHandle handle,
	                                          std::vector<std::uint32_t> instruction_indexes) const;

	//! Obtain the execution event that contains the transition whose id is specified
	//!
	//! Return nullopt if no such event exists, e.g. if the transition_id is greater than transition_count.
	std::experimental::optional<BlockExecutionEvent> event_at(std::uint64_t transition_id) const;

	//! Iterate on the execution event in the trace
	//!
	//! # Examples
	//!
	//! ```cpp
	//! for (auto& event : reader.query_events()) {
	//! 	InstructionBlock block = reader.block(event.block_handle);
	//! 	const bool instruction = event.has_instructions();
	//! 	const bool partial = block.instruction_count > event.execution_count();
	//!
	//! 	if (instruction) {
	//! 		std::cout << std::dec << "[" << event.begin_transition_id << "-" << event.end_transition_id << "]"
	//! 		          << " rip=" << std::hex << "0x" << block.first_pc
	//! 		          << " instruction_count=" << std::dec << block.instruction_count
	//! 		          << " partial=" << std::boolalpha << partial
	//! 		          << "\n";
	//! 	} else {
	//! 		std::cout << std::dec << "[" << event.begin_transition_id << "-" << event.end_transition_id << "]"
	//! 		          << " non-instruction\n";
	//! 	}
	//! }
	//! ```
	EventQuery query_events() const;

	//! Iterate on the transitions that are not instructions in the trace
	//!
	//! # Examples
	//! ```cpp
	//! for (std::uint64_t transition : reader.query_non_instructions()) {
	//! 	std::cout << std::dec << transition << "\n";
	//! }
	//! ```
	TransitionQuery query_non_instructions() const;

	//! Clear the cache, reclaiming the memory allocated by the cache.
	//!
	//! Warning: calling this method removes all block from the cache, invalidating any values returned by block or
	//! block_with_instructions.
	void clear_cache() const {
		cache_ = CacheMap{};
	}

	//! Retrieve the number of blocks currently contained in the cache.
	std::size_t cache_size() const {
		return cache_.size();
	}

private:
	using CacheMap = std::unordered_map<std::int64_t, InstructionBlock>;
	using CacheIterator = CacheMap::const_iterator;

	InstructionBlock fetch_from_db(BlockHandle handle) const;

	mutable sqlite::ResourceDatabase db_;
	mutable CacheMap cache_;

	mutable sqlite::Statement stmt_after_;
	mutable sqlite::Statement stmt_before_;
	mutable sqlite::Statement stmt_block_;
	mutable sqlite::Statement stmt_block_inst_;
};

}}} // namespace reven::block::reader
