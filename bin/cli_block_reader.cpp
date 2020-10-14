#include <block_reader.h>

#include <iostream>
#include <experimental/string_view>

using namespace reven::block;

void show_help_and_exit(const char* prog_name) {
	std::cerr << "Usage:\n";
	std::cerr << prog_name << " [filename]\n\n";
	std::cerr << "Reads the contents of a blocks database\n";
	std::cerr << "\t- filename: path to the blocks database, defaults to \"blocks.sqlite\"" << std::endl;
	std::exit(1);
}

const char* parse_args(int argc, char* argv[]) {
	if (argc < 2) {
		return "blocks.sqlite";
	}

	auto filename = argv[1];
	if (argc > 2 or std::experimental::string_view(filename) == "--help") {
		show_help_and_exit(argv[0]);
	}

	return filename;
}

int main(int argc, char* argv[]) {
	try {
		auto filename = parse_args(argc, argv);
		Reader reader(filename);

		std::cout << "Non-instructions" << std::endl;

		for (auto transition : reader.query_non_instructions()) {
			std::cout << std::dec << transition << "\n";
		}

		std::cout << "Finished Non-instructions" << std::endl;

		std::cout << "Execution trace" << std::endl;

		for (const auto& event : reader.query_events()) {
			const auto& block = reader.block(event.block_handle);
			const bool instruction = event.has_instructions();
			const bool partial = block.instruction_count > event.execution_count();

			if (instruction) {
				std::cout << std::dec << "[" << event.begin_transition_id << "-" << event.end_transition_id << "]"
				          << " rip=" << std::hex << "0x" << block.first_pc
				          << " instruction_count=" << std::dec << block.instruction_count
				          << " partial=" << std::boolalpha << partial
				          << "\n";
			} else {
				std::cout << std::dec << "[" << event.begin_transition_id << "-" << event.end_transition_id << "]"
				          << " non-instruction\n";
			}
		}

		std::cout << "Finished Execution trace" << std::endl;
	} catch (std::runtime_error& e) {
		std::cerr << "ERROR: " << e.what() << "\n\n";
		show_help_and_exit(argv[0]);
	}
	return 0;
}
