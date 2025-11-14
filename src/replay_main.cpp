#include "replay_engine.hpp"
#include "tsc_clock.hpp"
#include <iostream>
#include <string>
#include <memory>

int main(int argc, char* argv[]) {
    std::cout << R"(
    
      ARENA — ITCH 5.0 Replay Engine                         
      Zero-copy mmap → binary parse → LOB insert             
    
)" << std::endl;

    std::string itch_path;
    int generate_count = 0;

    if (argc >= 4 && std::string(argv[1]) == "--generate") {
        generate_count = std::atoi(argv[2]);
        itch_path = argv[3];
        std::cout << "[Mode] Generating " << generate_count << " synthetic ITCH messages\n\n";
        if (!arena::ITCHGenerator::generate(itch_path, generate_count, 100.0, 42)) {
            std::cerr << "Failed to generate ITCH data\n";
            return 1;
        }
        std::cout << "\n";
    } else if (argc >= 2) {
        itch_path = argv[1];
    } else {
        itch_path = "synthetic_itch.bin";
        generate_count = 100000;
        std::cout << "[Mode] Generating " << generate_count 
                  << " synthetic ITCH messages + replay\n\n";
        if (!arena::ITCHGenerator::generate(itch_path, generate_count, 100.0, 42)) {
            std::cerr << "Failed to generate ITCH data\n";
            return 1;
        }
        std::cout << "\n";
    }

    auto book = std::make_unique<arena::OrderBook>(0.01, 100.0);

    std::cout << "[Replay] Loading " << itch_path << "...\n";
    auto stats = arena::ReplayEngine::replay(itch_path, *book, 0);

    if (stats.parse_stats.messages_parsed == 0) {
        std::cerr << "\nNo messages parsed. Check file format.\n";
        return 1;
    }

    return 0;
}
