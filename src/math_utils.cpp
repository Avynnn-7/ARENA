#include "math_utils.hpp"
#include <cmath>
#include <iostream>
#include <iomanip>

namespace arena {

double normal_cdf(double x) {
    //since phi(x) is 1/2 * complementary error fn[-x/root 2] 
    return 0.5 * std::erfc(-x * 0.70710678118654752440);
}

double latency_log_variance(double mean, double stddev) {
    // sigma^2 = ln(1 + (s/m)^2) for L ~ LogNormal with mean m, std s.
    
    // safety check as latency must be positive
    if (mean <= 0.0) 
        return 0.0; 
    double cv2 = (stddev * stddev) / (mean * mean);
    return std::log1p(cv2); // is denoting ln(1 + cv^2)
}

double latency_log_mu(double mean, double stddev) {
    // mu = ln(m) - sigma^2 / 2
    if (mean <= 0.0) return 0.0; 
    return std::log(mean) - 0.5 * latency_log_variance(mean, stddev);
}

double expected_signal_decay(double mean, double std, double theta) {
    // No closed form solution for E[e^{-theta L}] so we integrate over the underlying normal Z ~ N(0,1):
    // E[e^{-theta L}] = ∫ phi(z) exp(-theta * exp(mu_L + sig_L z)) dz.
    // use quadrature method to find the value of the integral with quadrature error of ~1e-12 
    if (theta <= 0.0 || mean <= 0.0) return 1.0; 
    const double sig2 = latency_log_variance(mean, std);
    const double sig_L = std::sqrt(sig2);
    if (sig_L < 1e-12) {
        return std::exp(-theta * mean);
    }
    const double mu_L = latency_log_mu(mean, std);
    constexpr int N = 512;
    constexpr double zmax = 8.0;
    const double dz = (2.0 * zmax) / N;
    double num = 0.0;
    double den = 0.0;
    for (int k = 0; k < N; ++k) {
        const double z = -zmax + (k + 0.5) * dz;
        const double w = std::exp(-0.5 * z * z); 
        const double latency = std::exp(mu_L + sig_L * z);
        num += w * std::exp(-theta * latency);
        den += w;
    }
    return num / den; 

}

double compute_p_win(double mean_self, double std_self,
                     double mean_competitor, double std_competitor) {


    // Let a new r.v be Y s.t Y = ln(L_self) - ln(L_competitor) ~ N(mu_self - mu_comp, sig_self^2 + sig_comp^2).
    // P(self wins) = P(L_self < L_competitor) = P(Y < 0) = Phi((mu_comp - mu_self)/sd).

    // mean_self = m_self -> mean of Latency of trading bot called self i.e mean of actual latency which has Log normal dist
    // mu_self = u_self -> mean of Ln(latency) of trading bot called self i.e mean of natural log og latency which has normal dist
    // mean_self, std_self -> m_self,s_self ; mu_self,var_self -> u_self,sigma sq_self

    double var_self = latency_log_variance(mean_self, std_self);
    double var_comp = latency_log_variance(mean_competitor, std_competitor);
    double combined_std = std::sqrt(var_self + var_comp);
    if (combined_std < 1e-10) {

        // Special case : when Both ln(latencies) are effectively deterministic that is sigma_self and sigma_comp is very small then lower mean wins outright.

        if (mean_self < mean_competitor) return 1.0;
        if (mean_self > mean_competitor) return 0.0;
        return 0.5;
    }
    double mu_self = latency_log_mu(mean_self, std_self);
    double mu_comp = latency_log_mu(mean_competitor, std_competitor);
    return normal_cdf((mu_comp - mu_self) / combined_std);
}

double compute_joint_win_decay(double mean_self, double std_self,
                                double mean_competitor, double std_competitor,
                                double theta) {
    if (theta <= 0.0 || mean_self <= 0.0)
        return compute_p_win(mean_self, std_self, mean_competitor, std_competitor);

    const double sig2_A = latency_log_variance(mean_self, std_self);
    const double sig_A = std::sqrt(sig2_A);
    const double mu_A = latency_log_mu(mean_self, std_self);

    const double sig2_B = latency_log_variance(mean_competitor, std_competitor);
    const double sig_B = std::sqrt(sig2_B);
    const double mu_B = latency_log_mu(mean_competitor, std_competitor);

    if (sig_A < 1e-12) {
        double decay_val = std::exp(-theta * mean_self);
        double log_self = std::log(mean_self);
        double p_b_greater;
        if (sig_B < 1e-12) {
            p_b_greater = (mean_competitor > mean_self) ? 1.0
                        : (mean_competitor < mean_self) ? 0.0
                        : 0.5;
        } else {
            p_b_greater = 1.0 - normal_cdf((log_self - mu_B) / sig_B);
        }
        return decay_val * p_b_greater;
    }

    constexpr int N = 512;
    constexpr double zmax = 8.0;
    const double dz = (2.0 * zmax) / N;
    double num = 0.0;
    double den = 0.0;
    for (int k = 0; k < N; ++k) {
        const double z = -zmax + (k + 0.5) * dz;
        const double w = std::exp(-0.5 * z * z);
        const double log_latency_A = mu_A + sig_A * z;
        const double latency_A = std::exp(log_latency_A);
        const double decay_term = std::exp(-theta * latency_A);
        double p_b_greater;
        if (sig_B < 1e-12) {
            double log_comp = std::log(mean_competitor);
            p_b_greater = (log_latency_A < log_comp) ? 1.0
                        : (log_latency_A > log_comp) ? 0.0
                        : 0.5;
        } else {
            p_b_greater = 1.0 - normal_cdf((log_latency_A - mu_B) / sig_B);
        }
        num += w * decay_term * p_b_greater;
        den += w;
    }
    return num / den;
}

double compute_equilibrium_boundary(double mean_self, double std_self,
                                    double mean_competitor, double std_competitor,
                                    double theta, double mu, double cost_c) {

    // mu here means fair price that is say mu = 100 fair value then current value is b and mispricing is b - mu 
    double joint_win_decay = compute_joint_win_decay(mean_self, std_self, mean_competitor, std_competitor, theta);
    if (joint_win_decay < 1e-10) return 1.0; 

    // For optimal , put pi equals zero and calculate b star : P(win)*(b*-mu)*expected decay - c = 0  =>  b* = mu + c/(P*expected decay).

    return mu + cost_c / joint_win_decay;
}

double compute_solo_boundary(double mean_self, double std_self, double theta,
                             double mu, double cost_c) {
    
    // Just putting P(win) = 1 in above formula, we get boundary for solo
    
    double decay = expected_signal_decay(mean_self, std_self, theta);
    if (decay < 1e-10) return 1.0;
    return mu + cost_c / decay;
}

double compute_sharpe_ratio(const double* pnl_per_path, size_t n) {
    if (n < 2) return 0.0;

    // welford method - mean = 1/n * sigma xi and variance = 1/(n-1) * sigma(xi - mean)sq
    double mean = 0.0;
    double m2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double delta = pnl_per_path[i] - mean;
        mean += delta / static_cast<double>(i + 1);
        double delta2 = pnl_per_path[i] - mean;
        m2 += delta * delta2;
    }

    double variance = m2 / static_cast<double>(n - 1);
    double stddev = std::sqrt(variance);
    if (stddev < 1e-15) return 0.0;
    return mean / stddev;
}

double compute_lag1_autocorrelation(const double* series, size_t n) {
    if (n < 3) return 0.0;

    double mean = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mean += series[i];
    }
    mean /= static_cast<double>(n);

    double var_sum = 0.0;
    double cov_sum = 0.0;
    for (size_t i = 0; i < n - 1; ++i) {
        double dev_i = series[i] - mean;
        double dev_next = series[i + 1] - mean;
        var_sum += dev_i * dev_i;
        cov_sum += dev_i * dev_next;
    }
    double dev_last = series[n - 1] - mean;
    var_sum += dev_last * dev_last;

    if (var_sum < 1e-15) return 0.0;
    return cov_sum / var_sum;
}

/*

does not verify something new , just redoing the already done problems 

bool verify_equilibrium_convergence(double mean_A, double std_A,
                                    double mean_B, double std_B,
                                    double theta, double mu, double cost_c,
                                    double tol) {
    std::cout << std::fixed << std::setprecision(10);
    std::cout << "Boundary Indifference Verification:" << std::endl;

    double b_A = compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c);
    double b_B = compute_equilibrium_boundary(mean_B, std_B, mean_A, std_A, theta, mu, cost_c);

    double J_A = compute_joint_win_decay(mean_A, std_A, mean_B, std_B, theta);
    double J_B = compute_joint_win_decay(mean_B, std_B, mean_A, std_A, theta);
    double p_A = compute_p_win(mean_A, std_A, mean_B, std_B);
    double p_B = compute_p_win(mean_B, std_B, mean_A, std_A);

    double resid_A = J_A * (b_A - mu) - cost_c;
    double resid_B = J_B * (b_B - mu) - cost_c;

    std::cout << "  b_A* = " << b_A << " | P_A = " << p_A << " | J_A = " << J_A
              << " | residual = " << resid_A << std::endl;
    std::cout << "  b_B* = " << b_B << " | P_B = " << p_B << " | J_B = " << J_B
              << " | residual = " << resid_B << std::endl;

    bool ok = (std::abs(resid_A) < tol) && (std::abs(resid_B) < tol);
    if (ok) {
        std::cout << "  VERIFIED: both boundaries satisfy the zero-profit indifference"
                  << " condition (|residual| < " << tol << ")." << std::endl;
        std::cout << "  Note: b* is independent of the opponent's boundary, so this is"
                  << " also a dominant-strategy equilibrium." << std::endl;
    } else {
        std::cout << "  WARNING: indifference residual exceeds tolerance — the boundary"
                  << " formula and the payoff model are inconsistent." << std::endl;
    }
    return ok;
}

*/

} // namespace arena
