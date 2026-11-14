#include "signalscope/backend/dsp/parallel_channels.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace signalscope;
int main() {
    for (const auto workers : {1u, 4u}) {
        ParallelChannels pool(workers);
        std::array<unsigned, 37> seen{};
        for (unsigned batch = 0; batch < 10; ++batch)
            pool.run(seen.size(), [&](auto index) { ++seen[index]; });
        for (const auto count : seen)
            if (count != 10)
                return 1;
        bool failed = false;
        try {
            pool.run(seen.size(), [](auto index) {
                if (index == 7)
                    throw std::runtime_error("channel failure");
            });
        } catch (const std::runtime_error &) {
            failed = true;
        }
        if (!failed)
            return 2;
        pool.run(0, [](auto) { throw std::runtime_error("empty batch"); });
        pool.run(seen.size(), [&](auto index) { ++seen[index]; });
        for (const auto count : seen)
            if (count != 11)
                return 3;
    }
    std::cout << "Channel batches visit each lane once, synchronize results, propagate failure and remain reusable.\n";
}
