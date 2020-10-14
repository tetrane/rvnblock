#define BOOST_TEST_MODULE RVN_BINARY_TRACE_READER
#include <boost/test/unit_test.hpp>

#include <iostream>
#include <cstdint>

#include <block_writer.h>
#include <block_reader.h>

using namespace reven::block;

BOOST_AUTO_TEST_CASE(test_reader_base)
{
	auto db = []()
	{
		Writer writer(":memory:", "tester", "1.0.0", "BOOST AUTOTEST");

		ExecutedBlock block1;
		block1.block_instruction_count = 5;
		block1.mode = ExecutionMode::x86_64_bits;
		block1.pc = 0;
		std::vector<std::uint8_t> block1_data = {0, 1, 2, 3, 4, 5};
		writer.add_block(0, block1, Span{block1_data.size(), block1_data.data()});

		ExecutedBlock block2;
		block2.block_instruction_count = 7;
		block2.mode = ExecutionMode::x86_64_bits;
		block2.pc = 1;
		std::vector<std::uint8_t> block2_data = {0, 1, 2, 3, 4, 5};
		writer.add_block(5, block2, Span{block2_data.size(), block2_data.data()});

		ExecutedBlock block3;
		block3.block_instruction_count = 2;
		block3.mode = ExecutionMode::x86_64_bits;
		block3.pc = 2;
		std::vector<std::uint8_t> block3_data = {0, 1, 2, 3, 4, 5};
		writer.add_block(12, block3, Span{block3_data.size(), block3_data.data()});

		writer.finalize_execution(14);

		return std::move(writer).take();
	}();

	Reader reader(std::move(db));

	{
		auto maybe_event = reader.event_at(5);
		BOOST_CHECK(static_cast<bool>(maybe_event));
		BOOST_CHECK_EQUAL(maybe_event.value().begin_transition_id, 5);
		BOOST_CHECK_EQUAL(maybe_event.value().end_transition_id, 12);
		BOOST_CHECK_EQUAL(reader.block(maybe_event.value().block_handle).instruction_count, 7);
		BOOST_CHECK_EQUAL(reader.block(maybe_event.value().block_handle).first_pc, 1);
	}
	{
		auto maybe_event = reader.event_at(0);
		BOOST_CHECK(static_cast<bool>(maybe_event));
		BOOST_CHECK_EQUAL(maybe_event.value().begin_transition_id, 0);
		BOOST_CHECK_EQUAL(maybe_event.value().end_transition_id, 5);
		BOOST_CHECK_EQUAL(reader.block(maybe_event.value().block_handle).instruction_count, 5);
		BOOST_CHECK_EQUAL(reader.block(maybe_event.value().block_handle).first_pc, 0);
	}


}

// BOOST_CHECK_THROW(TraceReaderTester(s.reset().to_stream_with_cache_metadata("1.0.0")), IncompatibleTypeException);
