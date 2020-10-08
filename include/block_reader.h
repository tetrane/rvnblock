#pragma once

#include <cstdint>
#include <experimental/optional>
#include <unordered_map>
#include <vector>

#include "common.h"

#include <rvnsqlite/resource_database.h>

namespace reven {
namespace block {
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
};

}} // namespace reven::block
