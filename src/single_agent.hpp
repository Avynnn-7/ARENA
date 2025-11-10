#pragma once

#include <vector>
#include <random>

namespace arena {

struct AgentDecision {
    bool wants_to_act;
    double latency_drawn;
};

class SingleAgent {
public:
    // Real world Latency's actual MEAN and STANDARD DEVIATION inspire underlying log normal dist (mu, sigma) used for sampling
    SingleAgent(double mean_latency, double std_latency, double dt);

    /*  Inspired from Budish Cramton Shim(BCS) information model: the agent observes V in real time
        Latency delays only order arrival at the market not market data delivery,thus
        avoiding an artificial double delay in information recieved and its execution
    */
    template<typename RNG>
    AgentDecision evaluate_action(const double* v_data, int v_size, int current_step, double stopping_boundary, RNG& rng) {
        double delta = lognormal_dist_(rng);            // order transmission latency
        int idx = current_step;
        if (idx < 0) idx = 0;
        if (idx >= v_size) idx = v_size - 1;
        double observed_y = v_data[idx];                // real time observation
        bool acts = observed_y >= stopping_boundary;
        return {acts, delta};
    }

    template<typename RNG>
    AgentDecision evaluate_action(const std::vector<double>& v_history, int current_step, double stopping_boundary, RNG& rng) {
        return evaluate_action(v_history.data(), static_cast<int>(v_history.size()), current_step, stopping_boundary, rng);
    }

private:
    double mean_latency_;
    double std_latency_;
    double dt_;
    std::lognormal_distribution<double> lognormal_dist_;
};

} // namespace arena
