#include <block_reader.h>

#include "common.h"

#include <rvnmetadata/metadata.h>

namespace reven {
namespace block {
namespace reader {

Reader::Reader(const char* filename) :
    Reader(sqlite::ResourceDatabase::open(filename, true))
{

}

Reader::Reader(sqlite::ResourceDatabase db) :
    db_(std::move(db)),
    stmt_after_(db_, "SELECT transition_id, block_id FROM execution "
                     "WHERE transition_id > ? "
                     "ORDER BY transition_id ASC "
                     "LIMIT 1"
                     ";"),
    stmt_before_(db_, "SELECT transition_id FROM execution "
                      "WHERE transition_id <= ? "
                      "ORDER BY transition_id DESC "
                      "LIMIT 1"
                      ";"),
    stmt_block_(db_, "SELECT pc, instruction_data, instruction_count, mode "
                     "FROM blocks WHERE rowid = ?"
                     ";"),
    stmt_block_inst_(db_, "SELECT instruction_index "
                          "FROM instruction_indices WHERE block_id = ? "
                          "ORDER BY instruction_id ASC"
                          ";"),
    stmt_interrupt_at_(db_, "SELECT pc, mode, number, is_hw, related_instruction_block_id "
                            "FROM interrupts WHERE transition_id = ? "
                            ";")
{
	const auto md = metadata::Metadata::from_raw_metadata(db_.metadata());
	if (md.type() != metadata::ResourceType::Block) {
		throw std::runtime_error("Cannot open a resource of type " +
		                         metadata::to_string(md.type()).to_string());
	}

	const auto cmp = md.format_version().compare(metadata::Version::from_string(format_version));

	if (not cmp.is_compatible()) {
		if (cmp.detail < metadata::Version::Comparison::Current) {
			throw std::runtime_error("Incompatible version " + md.format_version().to_string() + ": Past version");
		} else {
			throw std::runtime_error("Incompatible version " + md.format_version().to_string() + ": Future version");
		}
	}

	try {
		auto interrupt = block(BlockHandle::interrupt_block_handle());
		auto interrupt_msg = std::string(reinterpret_cast<char*>(interrupt.instruction_data.data()),
		                                 interrupt.instruction_data.size());
		if (interrupt_msg != "interrupt") {
			throw std::runtime_error("First block is not a valid interrupt block.");
		}
	} catch (std::runtime_error& e) {
		throw std::runtime_error(std::string("Could not find interrupt block: ") + e.what());
	}
}

const InstructionBlock& Reader::block(BlockHandle handle) const
{
	auto itbool = cache_.insert({handle.handle_, {}});
	if (itbool.second) {
		try {
			itbool.first->second= fetch_from_db(handle);
		} catch (std::runtime_error&) {
			cache_.erase(itbool.first);
			throw;
		}
	}

	return itbool.first->second;
}

BlockInstructions Reader::block_with_instructions(BlockHandle handle,
                                                  std::vector<std::uint32_t> instruction_indexes) const
{
	const auto& db_block = block(handle);
	if (db_block.instruction_count == 0) {
		return BlockInstructions(db_block, {});
	}

	stmt_block_inst_.reset();
	stmt_block_inst_.bind_arg(1, handle.handle_, "rowid");
	instruction_indexes.clear();
	instruction_indexes.reserve(db_block.instruction_count);
	while (stmt_block_inst_.step() == sqlite::Statement::StepResult::Row) {
		std::uint32_t instruction_index = stmt_block_inst_.column_u32(0);
		instruction_indexes.push_back(instruction_index);
	}
	return BlockInstructions(db_block, std::move(instruction_indexes));
}

std::experimental::optional<BlockExecutionEvent> Reader::event_at(uint64_t transition_id) const
{
	// find next block
	stmt_after_.reset();
	stmt_after_.bind_arg_throw(1, transition_id, "transition_id");
	if (stmt_after_.step() == sqlite::Statement::StepResult::Done) {
		return {};
	}

	std::uint64_t end_transition_id = stmt_after_.column_u64(0);
	std::int32_t block_id = stmt_after_.column_i32(1);

	// find block right before the current transition
	stmt_before_.reset();
	std::uint64_t begin_transition_id = 0;
	stmt_before_.bind_arg_throw(1, transition_id, "transition_id");
	if (stmt_before_.step() == sqlite::Statement::StepResult::Row) {
		begin_transition_id = stmt_before_.column_u64(0);
	} // else block_begin remains at 0;

	return BlockExecutionEvent{begin_transition_id, end_transition_id, BlockHandle{block_id}};
}

std::experimental::optional<Interrupt> Reader::interrupt_at(std::uint64_t transition_id) const
{
	stmt_interrupt_at_.reset();
	stmt_interrupt_at_.bind_arg_throw(1, transition_id, "transition_id");
	if (stmt_interrupt_at_.step() == sqlite::Statement::StepResult::Done) {
		return {};
	}

	std::uint64_t pc = stmt_interrupt_at_.column_u64(0);
	ExecutionMode mode = static_cast<ExecutionMode>(stmt_interrupt_at_.column_i32(1));
	std::int32_t number = stmt_interrupt_at_.column_i32(2);
	bool is_hw = stmt_interrupt_at_.column_i32(3) != 0;
	BlockHandle block_handle = BlockHandle{ stmt_interrupt_at_.column_i32(4) };
	return Interrupt(pc, mode, number, is_hw, block_handle);
}

std::experimental::optional<Span> Reader::related_instruction_data(const Interrupt& interrupt) const
{
	if (not interrupt.has_related_instruction()) {
		return {};
	}

	auto& db_block = block(interrupt.handle_);

	std::uint64_t interrupt_offset = interrupt.pc - db_block.first_pc;

	std::uint64_t begin = 0;

	stmt_block_inst_.reset();
	stmt_block_inst_.bind_arg(1, interrupt.handle_.handle_, "rowid");
	while (stmt_block_inst_.step() == sqlite::Statement::StepResult::Row) {
		std::uint32_t end = stmt_block_inst_.column_u32(0);

		if (begin == interrupt_offset) {
			std::size_t size = end - begin;
			auto* data = db_block.instruction_data.data() + begin;
			return Span{size, data};
		}

		begin = end;
	}

	// At this point we are at the last possible offset
	if (begin == interrupt_offset) {
		std::uint64_t end = db_block.instruction_data.size();
		std::size_t size = end - begin;
		// If we never executed the entire block, we may mistakenly take bytes from instructions further in this block.
		// Without a disassembler, we have absolutely no way of distinguishing where to end the instruction,
		// so (like in the existing context-based transition implementation), we will have to take more bytes.
		// For performance reasons, we limit this to the maximal number of bytes a x86 instruction can contain: 15.
		if (size > 15) {
			size = 15;
		}
		auto* data = db_block.instruction_data.data() + begin;
		return Span{size, data};
	}

	return {};
}

struct EventQueryState {
	std::uint64_t previous_transition_id = 0;

	BlockExecutionEvent operator()(sqlite::Statement& stmt) {
		std::uint64_t end_transition_id = stmt.column_u64(0);
		std::int32_t block_id = stmt.column_i32(1);

		auto event = BlockExecutionEvent{previous_transition_id, end_transition_id, BlockHandle{block_id}};
		previous_transition_id = end_transition_id;
		return event;
	}
};

Reader::EventQuery Reader::query_events() const
{
	sqlite::Statement stmt(db_, "SELECT transition_id, block_id FROM execution ORDER BY transition_id ASC;");

	return EventQuery(std::move(stmt), EventQueryState{});
}

Reader::TransitionQuery Reader::query_non_instructions() const
{
	sqlite::Statement stmt(db_, "SELECT transition_id FROM execution WHERE block_id = 1 ORDER BY transition_id ASC;");

	return TransitionQuery(std::move(stmt), [](auto& stmt) -> std::uint64_t {
		std::uint64_t next_transition_id = stmt.column_u64(0);
		if (next_transition_id == 0) {
			return 0;
		}
		return next_transition_id - 1;
	});
}

InstructionBlock Reader::fetch_from_db(BlockHandle handle) const
{
	stmt_block_.reset();
	stmt_block_.bind_arg(1, handle.handle_, "rowid");
	if (stmt_block_.step() != sqlite::Statement::StepResult::Row) {
		throw std::runtime_error("Unknown block_id");
	}

	auto pc = stmt_block_.column_u64(0);
	auto inst_data = stmt_block_.column_blob(1);
	const uint8_t* inst_data_buf = reinterpret_cast<const uint8_t*>(std::get<0>(inst_data));
	std::size_t inst_data_size = std::get<1>(inst_data);
	std::uint16_t inst_count = stmt_block_.column_i32(2);
	ExecutionMode mode = static_cast<ExecutionMode>(stmt_block_.column_i32(3));

	return InstructionBlock{{inst_data_buf, inst_data_buf + inst_data_size}, pc, inst_count, mode};
}

metadata::Version Reader::resource_version()
{
	return metadata::Version::from_string(format_version);
}

metadata::ResourceType Reader::resource_type()
{
	return reven::metadata::ResourceType::Block;
}

}}} // namespace reven::block::reader
