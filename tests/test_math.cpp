#include <gtest/gtest.h>
#include "ou_sampler.hpp"
#include "math_utils.hpp"
#include <random>
#include <cmath>

TEST(OUSamplerTest, StationaryMoments) {
    double theta = 2.0;
    double mu = 100.0;
    double sigma_V = 5.0;
    double dt = 0.01;
    int num_steps = 100000;
    
    arena::OUSampler sampler(theta, mu, sigma_V, dt);
    std::mt19937_64 rng(42); 
    
    // Burn-in
    double v_t = mu;
    for (int i = 0; i < 10000; ++i) {
        v_t = sampler.step(v_t, rng);
    }
    
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < num_steps; ++i) {
        v_t = sampler.step(v_t, rng);
        sum += v_t;
        sum_sq += v_t * v_t;
    }
    
    double empirical_mean = sum / num_steps;
    double empirical_var = (sum_sq / num_steps) - (empirical_mean * empirical_mean);
    
    double theoretical_mean = sampler.get_stationary_mean();
    double theoretical_var = sampler.get_stationary_variance();
    
    EXPECT_NEAR(empirical_mean, theoretical_mean, 0.1);
    EXPECT_NEAR(empirical_var, theoretical_var, theoretical_var * 0.05);
}

TEST(MathUtilsTest, ComputePWin) {
    const double m = 0.02; 
    EXPECT_DOUBLE_EQ(arena::compute_p_win(m, 0.004, m, 0.004), 0.5);

    // Equal mean, self has LOWER jitter => win probability < 0.5 (wins less).
    EXPECT_LT(arena::compute_p_win(m, 0.002, m, 0.008), 0.5);

    // Equal mean, self has HIGHER jitter => win probability > 0.5 (wins more).
    EXPECT_GT(arena::compute_p_win(m, 0.008, m, 0.002), 0.5);

    // A genuine mean latency (speed) edge dominates -> a faster mean beats a
    // slower competitor regardless of the jitter trade off.
    EXPECT_GT(arena::compute_p_win(0.01, 0.003, 0.02, 0.002), 0.5);
}

TEST(MathUtilsTest, ComputeEquilibriumBoundary) {
    double mean_A = 0.02, std_A = 0.002;  // tight jitter
    double mean_B = 0.02, std_B = 0.008;  // loose jitter
    double theta = 2.0;
    double mu = 0.0;
    double cost_c = 0.05; 

    double b_A = arena::compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
    double b_B = arena::compute_equilibrium_boundary(mean_B, std_B, mean_A, std_A, theta, mu, cost_c);

    // Equal mean => equal signal decay. A has the TIGHTER jitter so under
    // convention A it wins the race LESS often i.e (P(win) < 0.5) and must therefore
    // demand a LARGER signal before firing: b_A > b_B.
    EXPECT_GT(b_A, b_B);
    EXPECT_GT(b_A, cost_c);
}

TEST(MathUtilsTest, ExpectedSignalDecayLaplace) {
    double theta = 2.0;
    double mean = 0.02;

    EXPECT_NEAR(arena::expected_signal_decay(mean, 0.0, theta),
                std::exp(-theta * mean), 1e-9);

    double decay_disp = arena::expected_signal_decay(mean, 0.01, theta);
    EXPECT_GT(decay_disp, std::exp(-theta * mean));

    EXPECT_GT(decay_disp, 0.0);
    EXPECT_LE(decay_disp, 1.0);

    EXPECT_GT(arena::expected_signal_decay(mean, 0.02, theta),
              arena::expected_signal_decay(mean, 0.005, theta));
}

TEST(MathUtilsTest, BoundaryIndifferenceIdentity) {
    // The boundary is DEFINED by P(win)*(b*-mu)*decay - cost_c = 0
    double mean_A = 0.02, std_A = 0.006;
    double mean_B = 0.02, std_B = 0.003;
    double theta = 2.0, mu = 0.0, cost_c = 0.05;

    double b = arena::compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
    double J = arena::compute_joint_win_decay(mean_A, std_A, mean_B, std_B, theta);
    EXPECT_NEAR(J * (b - mu) - cost_c, 0.0, 1e-9);

    double b_solo = arena::compute_solo_boundary(mean_A, std_A, theta, mu, cost_c);
    double d = arena::expected_signal_decay(mean_A, std_A, theta);
    EXPECT_NEAR(1.0 * (b_solo - mu) * d - cost_c, 0.0, 1e-9);
}
