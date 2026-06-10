#pragma once

#include <random>

namespace arena {

struct RaceResult {
    bool agent_a_won;
    bool agent_b_won;
};

// figures out who wins when both model try to snipe at the same time 

class RaceResolver {
public:

    // lowest latency wins the race that is whoever reaches first wins ofc
    RaceResult resolve_race(double delta_a, double delta_b) const {
        if (delta_a < delta_b) {
            return {true, false};
        } else if (delta_b < delta_a) {
            return {false, true};
        }

        /*
            In continuous time two latencies are never exactly equal (prob = 0 in cont time),
            but doubles only carry ~15 digits so two distinct draws can collapse
            onto the same value. They still can't physically arrive together so we flip a fair coin
            and let one of them take it.
        */

        thread_local std::mt19937 rng{std::random_device{}()};
        thread_local std::bernoulli_distribution coin{0.5};
        return coin(rng) ? RaceResult{true, false} : RaceResult{false, true};
    }
};

} // namespace arena
