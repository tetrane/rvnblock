#define BOOST_TEST_MODULE RVN_BINARY_TRACE_READER
#include <boost/test/unit_test.hpp>

#include <iostream>
#include <cstdint>

#include <block_writer.h>
#include <block_reader.h>

using namespace reven::block;
using Writer = writer::Writer;
using ExecutedBlock = writer::ExecutedBlock;
using Reader = reader::Reader;

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

BOOST_AUTO_TEST_CASE(test_reader_indices)
{
	auto db = []()
	{
		Writer writer(":memory:", "tester", "1.0.0", "BOOST AUTOTEST");

		ExecutedBlock block1;
		block1.block_instruction_count = 5;
		block1.mode = ExecutionMode::x86_64_bits;
		block1.pc = 0;
		std::vector<std::uint8_t> block1_data = {0, 1, 2, 3, 4, 42};
		writer.add_block(0, block1, Span{block1_data.size(), block1_data.data()});
		writer.add_block_instruction(0);
		writer.add_block_instruction(2);
		writer.add_block_instruction(3);
		writer.add_block_instruction(4);
		writer.add_block_instruction(5);

		ExecutedBlock block2;
		block2.block_instruction_count = 2;
		block2.mode = ExecutionMode::x86_64_bits;
		block2.pc = 200;
		std::vector<std::uint8_t> block2_data = {0, 1, 2, 3, 4, 5};
		writer.add_block(5, block2, Span{block2_data.size(), block2_data.data()});
		writer.add_block_instruction(200);
		writer.add_block_instruction(205);

		writer.finalize_execution(7);

		return std::move(writer).take();
	}();

	Reader reader(std::move(db));

	{
		auto maybe_event = reader.event_at(6);
		BOOST_CHECK(static_cast<bool>(maybe_event));
		BOOST_CHECK_EQUAL(maybe_event.value().begin_transition_id, 5);
		BOOST_CHECK_EQUAL(maybe_event.value().end_transition_id, 7);
		BOOST_CHECK_EQUAL(reader.block(maybe_event.value().block_handle).instruction_count, 2);
		BOOST_CHECK_EQUAL(reader.block(maybe_event.value().block_handle).first_pc, 200);
		BOOST_CHECK_EQUAL(reader.block_with_instructions(maybe_event.value().block_handle, {}).instruction_count(), 2);
		BOOST_CHECK_EQUAL(reader.block_with_instructions(maybe_event.value().block_handle,
		                  {}).instruction(1).value().pc, 205);
	}
	{
		auto maybe_event = reader.event_at(3);
		BOOST_CHECK(static_cast<bool>(maybe_event));
		BOOST_CHECK_EQUAL(maybe_event.value().begin_transition_id, 0);
		BOOST_CHECK_EQUAL(maybe_event.value().end_transition_id, 5);
		BOOST_CHECK_EQUAL(reader.block(maybe_event.value().block_handle).instruction_count, 5);
		BOOST_CHECK_EQUAL(reader.block(maybe_event.value().block_handle).first_pc, 0);
		BOOST_CHECK_EQUAL(reader.block_with_instructions(maybe_event.value().block_handle, {}).instruction_count(), 5);
		BOOST_CHECK_EQUAL(reader.block_with_instructions(maybe_event.value().block_handle,
		                  {}).instruction(0).value().pc, 0);
		BOOST_CHECK_EQUAL(reader.block_with_instructions(maybe_event.value().block_handle,
		                  {}).instruction(1).value().pc, 2);
		BOOST_CHECK_EQUAL(reader.block_with_instructions(maybe_event.value().block_handle,
		                  {}).instruction(4).value().pc, 5);
		BOOST_CHECK_EQUAL(*(reader.block_with_instructions(maybe_event.value().block_handle,
		                  {}).instruction(4).value().data.data), 42);
	}
}

BOOST_AUTO_TEST_CASE(test_reader_interrupt)
{
	auto db = []()
	{
		Writer writer(":memory:", "tester", "1.0.0", "BOOST AUTOTEST");

		ExecutedBlock block1;
		block1.block_instruction_count = 5;
		block1.mode = ExecutionMode::x86_64_bits;
		block1.pc = 0;
		std::vector<std::uint8_t> block1_data = {0, 1, 2, 3, 4, 42};
		for (int i = 0; i < 72; ++i) {
			block1_data.push_back(23);
		}
		writer.add_block(0, block1, Span{block1_data.size(), block1_data.data()});
		writer.add_block_instruction(0);
		writer.add_block_instruction(2);
		writer.add_block_instruction(3);
		writer.add_block_instruction(4);
		reven::block::writer::Interrupt interrupt;
		interrupt.has_related_instruction = true;
		interrupt.is_hw = false;
		interrupt.mode = ExecutionMode::x86_64_bits;
		interrupt.number = 14;
		interrupt.pc = 4;
		writer.add_interrupt(3, interrupt);


		ExecutedBlock block2;
		block2.block_instruction_count = 2;
		block2.mode = ExecutionMode::x86_32_bits;
		block2.pc = 200;
		std::vector<std::uint8_t> block2_data = {0, 1, 2, 3, 4, 5};
		writer.add_block(5, block2, Span{block2_data.size(), block2_data.data()});
		writer.add_block_instruction(200);
		interrupt.has_related_instruction = true;
		interrupt.is_hw = false;
		interrupt.mode = ExecutionMode::x86_32_bits;
		interrupt.number = 14;
		interrupt.pc = 205;
		writer.add_interrupt(6, interrupt);

		writer.add_block(7, block2, Span{block2_data.size(), block2_data.data()});
		writer.add_block_instruction(200);
		writer.add_block_instruction(205);

		writer.add_block(9, block2, Span{block2_data.size(), block2_data.data()});
		interrupt.has_related_instruction = false;
		interrupt.is_hw = true;
		interrupt.mode = ExecutionMode::x86_32_bits;
		interrupt.number = 209;
		interrupt.pc = 200;
		writer.add_interrupt(9, interrupt);

		writer.add_block(10, block2, Span{block2_data.size(), block2_data.data()});
		writer.add_block_instruction(200);
		writer.add_block_instruction(205);

		writer.finalize_execution(12);

		return std::move(writer).take();
	}();

	Reader reader(std::move(db));

	{
		BOOST_CHECK_EQUAL(reader.interrupt_at(3).value().is_hw, false);
		BOOST_CHECK_EQUAL(reader.interrupt_at(3).value().has_related_instruction(), true);
		BOOST_CHECK(reader.interrupt_at(3).value().mode == ExecutionMode::x86_64_bits);
		BOOST_CHECK_EQUAL(reader.interrupt_at(3).value().number, 14);
		BOOST_CHECK_EQUAL(reader.interrupt_at(3).value().pc, 4);

		BOOST_CHECK_EQUAL(reader.related_instruction_data(reader.interrupt_at(3).value()).value().size, 15);
		BOOST_CHECK_EQUAL(reader.related_instruction_data(reader.interrupt_at(3).value()).value().data[0], 4);
		BOOST_CHECK_EQUAL(reader.related_instruction_data(reader.interrupt_at(3).value()).value().data[1], 42);
		BOOST_CHECK_EQUAL(reader.related_instruction_data(reader.interrupt_at(3).value()).value().data[2], 23);

		BOOST_CHECK_EQUAL(reader.interrupt_at(6).value().is_hw, false);
		BOOST_CHECK_EQUAL(reader.interrupt_at(6).value().has_related_instruction(), true);
		BOOST_CHECK(reader.interrupt_at(6).value().mode == ExecutionMode::x86_32_bits);
		BOOST_CHECK_EQUAL(reader.interrupt_at(6).value().number, 14);
		BOOST_CHECK_EQUAL(reader.interrupt_at(6).value().pc, 205);

		BOOST_CHECK_EQUAL(reader.related_instruction_data(reader.interrupt_at(6).value()).value().size, 1);
		BOOST_CHECK_EQUAL(reader.related_instruction_data(reader.interrupt_at(6).value()).value().data[0], 5);


		BOOST_CHECK_EQUAL(reader.interrupt_at(9).value().is_hw, true);
		BOOST_CHECK_EQUAL(reader.interrupt_at(9).value().has_related_instruction(), false);
		BOOST_CHECK(reader.interrupt_at(6).value().mode == ExecutionMode::x86_32_bits);
		BOOST_CHECK_EQUAL(reader.interrupt_at(9).value().number, 209);
		BOOST_CHECK_EQUAL(reader.interrupt_at(9).value().pc, 200);

		BOOST_CHECK(not reader.related_instruction_data(reader.interrupt_at(9).value()));
	}
}
