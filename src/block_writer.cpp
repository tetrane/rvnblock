#include <block_writer.h>

#include <rvnmetadata/metadata.h>

#include <boost/uuid/sha1.hpp>

namespace reven {
namespace block {

namespace {
// see boost::uuids::sha1::digest_type
constexpr std::size_t DIGEST_SIZE = 5;

using Db = sqlite::Database;
using RDb = sqlite::ResourceDatabase;
using Stmt = sqlite::Statement;

using Meta = ::reven::metadata::Metadata;
using MetaType = ::reven::metadata::ResourceType;
using MetaVersion = ::reven::metadata::Version;

void create_sqlite_db(Db& db)
{
	db.exec("create table blocks("
	        "pc int8 not null,"
	        "instruction_data blob not null,"
	        "instruction_count int2 not null,"
	        "mode int1 not null"
	        ");",
	        "Can't create table blocks");
	db.exec("create table execution("
	        "transition_id int8 PRIMARY KEY not null,"
	        "block_id int4 not null"
	        ") WITHOUT ROWID;",
	        "Can't create table execution");
	db.exec("CREATE TABLE instruction_indices("
	        "block_id INTEGER NOT NULL,"
	        "instruction_id INTEGER NOT NULL,"
	        "instruction_index INTEGER NOT NULL,"
	        "PRIMARY KEY (block_id, instruction_id)"
	        ") WITHOUT ROWID;",
	        "Can't create table instruction_indices");

	db.exec("pragma synchronous=off", "Pragma error");
	db.exec("pragma count_changes=off", "Pragma error");
	db.exec("pragma journal_mode=memory", "Pragma error");
	db.exec("pragma temp_store=memory", "Pragma error");
}

ExecutedBlock interrupt_block() {
	return ExecutedBlock{0, 0, ExecutionMode::x86_64_bits};
}

Span interrupt_data() {
	static std::string interrupt_msg("interrupt");
	return Span{interrupt_msg.size(), reinterpret_cast<const std::uint8_t*>(interrupt_msg.data())};
}
} // anonymous namespace


void reven::block::Writer::reset_last_block(ExecutedBlock block, unsigned int* digest, reven::block::Span instruction_data)
{
	last_instruction_data_.clear();
	last_instruction_data_.insert(last_instruction_data_.end(),instruction_data.data,
	                              instruction_data.data + instruction_data.size);
	last_block_ = block;
	last_id_ = 0;
	last_hash_.clear();
	last_block_instruction_indices_.clear();

	last_hash_.insert(last_hash_.end(), digest, digest + DIGEST_SIZE);
}

void Writer::insert_last_block()
{
	// check with all previously inserted blocks
	auto itbool = block_map_.insert({last_hash_, MappedBlock{0, 0, last_block_}});
	auto& value = itbool.first->second;
	if (itbool.second) {
		// Is a new block
		last_id_ = insert_block_db(last_block_, Span{last_instruction_data_.size(), last_instruction_data_.data()});


		value.id = last_id_;
	} else {
		// Existing block, get back block ID
		if (last_block_ != value.block) {
			throw std::runtime_error("Collision between blocks");
		}
		last_id_ = value.id;
	}

	if (value.executed_instructions < last_block_instruction_indices_.size()) {
		insert_executed_instructions_db(last_block_instruction_indices_, value.executed_instructions);
		value.executed_instructions = last_block_instruction_indices_.size();
	}
}

std::int64_t Writer::insert_block_db(const ExecutedBlock& block, Span instruction_data)
{
	last_block_stmt_.bind_arg_cast(1, block.pc, "pc");
	last_block_stmt_.bind_blob_without_copy(2, instruction_data.data, instruction_data.size,
	                                        "instruction_data");
	last_block_stmt_.bind_arg(3, block.block_instruction_count, "instruction_count");
	last_block_stmt_.bind_arg(4, static_cast<std::uint8_t>(block.mode), "mode");

	step_transaction(last_block_stmt_);
	last_block_stmt_.reset();

	return db_.last_insert_rowid();
}

void Writer::insert_executed_instructions_db(const std::vector<uint32_t>& block_instruction_indices,
                                             uint32_t already_inserted_instructions)
{
	for (std::uint32_t instruction_id = already_inserted_instructions;
	     instruction_id < block_instruction_indices.size(); ++instruction_id) {
		const auto instruction_index = block_instruction_indices[instruction_id];

		instructions_stmt_.bind_arg(1, last_id_, "block_id");
		instructions_stmt_.bind_arg_cast(2, instruction_id, "instruction_id");
		instructions_stmt_.bind_arg_cast(3, instruction_index, "index");

		step_transaction(instructions_stmt_);
		instructions_stmt_.reset();
	}
}

void Writer::insert_block_execution(std::uint64_t transition_id)
{
	block_execution_stmt_.bind_arg_throw(1, transition_id, "transition_id");
	block_execution_stmt_.bind_arg(2, last_id_, "block_id");
	step_transaction(block_execution_stmt_);
	block_execution_stmt_.reset();
	last_transition_id_ = transition_id;
}

Stmt::StepResult Writer::step_transaction(sqlite::Statement& stmt)
{
	if (transaction_items_ == 0) {
		db_.exec("begin", "Cannot start transaction");
	}
	++transaction_items_;
	if (transaction_items_ > TRANSACTION_COUNT) {
		transaction_items_ = 0;
		db_.exec("commit", "Cannot commit transaction");
	}
	return stmt.step();
}

Writer::Writer(const char* filename, const char* tool_name,
               const char* tool_version,
               const char* tool_info) :
    db_([filename, tool_name, tool_version, tool_info]() {
	auto md = Meta(MetaType::Block, MetaVersion::from_string(format_version), tool_name, MetaVersion::from_string(tool_version),
	               tool_info + std::string(" - using rvnblock ") + writer_version);
	auto rdb = RDb::create(filename, md.to_sqlite_raw_metadata());
	create_sqlite_db(rdb);
	return rdb;
}()),
    last_block_stmt_(db_, "insert into blocks values (?, ?, ?, ?);"),
    instructions_stmt_(db_, "INSERT INTO instruction_indices VALUES (?, ?, ?);"),
    block_execution_stmt_(db_, "insert into execution values (?, ?);")
{
	// insert interrupt block
	// see boost::uuids::sha1::digest_type
	unsigned int digest[DIGEST_SIZE];

	auto block = interrupt_block();
	auto sha1 = boost::uuids::detail::sha1();
	sha1.process_bytes(&block, sizeof(block));
	sha1.process_bytes(interrupt_data().data, interrupt_data().size);
	sha1.get_digest(digest);

	Hash hash;
	hash.insert(hash.end(), digest, digest + DIGEST_SIZE);

	auto block_id = insert_block_db(block, interrupt_data());
	block_map_.insert({hash, MappedBlock{block_id, 0, block}});
}

Writer::~Writer()
{
	if (db_.get() == nullptr) {
		return;
	}

	if (transaction_items_ != 0) {
		db_.exec("commit", "Cannot commit transaction");
	}
}

void Writer::add_block(uint64_t current_transition, ExecutedBlock block, Span instruction_data)
{
	// see boost::uuids::sha1::digest_type
	unsigned int digest[DIGEST_SIZE];

	auto sha1 = boost::uuids::detail::sha1();
	sha1.process_bytes(&block, sizeof(block));
	sha1.process_bytes(instruction_data.data, instruction_data.size);
	sha1.get_digest(digest);

	// first block
	if (last_hash_.size() != DIGEST_SIZE) {
		if (not last_hash_.empty()) {
			throw std::logic_error("Unexpected non-empty last_hash");
		}

		reset_last_block(block, digest, instruction_data);
		return;
	}

	if (current_transition != last_transition_id_) {
		insert_last_block();
		insert_block_execution(current_transition);
	}

	reset_last_block(block, digest, instruction_data);
}

void Writer::add_block_instruction(uint64_t rip)
{
	if (last_hash_.size() != DIGEST_SIZE) {
		throw std::logic_error("Call to add_block_instruction before any call to add_block");
	}
	std::uint32_t index = rip - last_block_.pc;
	if (index == 0) {
		return;
	}
	last_block_instruction_indices_.push_back(index);
}

void Writer::add_interrupt(uint64_t current_transition)
{
	add_block(current_transition, interrupt_block(), interrupt_data());
}

void Writer::finalize_execution(uint64_t last_transition_id)
{
	if (last_hash_.size() == DIGEST_SIZE and
	    last_transition_id != last_transition_id_) {
		insert_last_block();
		insert_block_execution(last_transition_id);
	}
}

sqlite::ResourceDatabase Writer::take() &&
{
	if (db_.get() != nullptr) {
		if (transaction_items_ != 0) {
			db_.exec("commit", "Cannot commit transaction");
		}
	}

	return std::move(db_);
}

}} // namespace reven::block
