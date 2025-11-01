#pragma once

#include <cstddef>

namespace arena {

// Latencies L_i are parameterised as Log Normal due to positive and right skewed nature of them. 
// Ln(L) ~ Normal(mu, sigma) ; E[L] = m ; var(L) = s square
// m = exp(mu + sigma^2 / 2) 
// s = m * sqrt(exp(sigma^2) - 1) 
// sigma^2 = ln(1 + (s/m)^2)
// mu= ln(m) - sigma^2 / 2

double latency_log_variance(double mean, double stddev);
double latency_log_mu(double mean, double stddev);

double normal_cdf(double x);

double expected_signal_decay(double mean, double std, double theta);

double compute_p_win(double mean_self, double std_self,
                     double mean_competitor, double std_competitor);

double compute_joint_win_decay(double mean_self, double std_self,
                                double mean_competitor, double std_competitor,
                                double theta);

double compute_equilibrium_boundary(double mean_self, double std_self,
                                    double mean_competitor, double std_competitor,
                                    double theta, double mu, double cost_c);

double compute_solo_boundary(double mean_self, double std_self, double theta,
                             double mu, double cost_c);

double compute_sharpe_ratio(const double* pnl_per_path, size_t n);

// lag-1 auto-correlation to check the sampler is working
double compute_lag1_autocorrelation(const double* series, size_t n);

/*
This is just a substantive algebraic check, not a tautological one.
bool verify_equilibrium_convergence(double mean_A, double std_A,
                                    double mean_B, double std_B,
                                    double theta, double mu, double cost_c,
                                    double tol = 1e-9);

*/

} // namespace arena
