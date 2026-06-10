#pragma once

#include <random>
#include <cmath>
#include <stdexcept>

namespace arena {

// tho using namespace std isnt a respected method but still utilising it inside arena to make the code more readable

using namespace std;

/*
 * exact OU sampler instead of euler-maruyama to prevent discretization errors.

 * included everything directly into hpp instead of splitting into hpp and cpp as the code structure was quite small and can exploit
 * use of cpp inline function to increase execution speed by bypassing function call
 */


class OUSampler {
public:
    // Constructor: called once when object is created to pre calculate math like exponents and square roots and prevent computation later
    OUSampler(double theta, double mu, double sigma_V, double dt)

        : mu_(mu), norm_dist_(0.0, 1.0) {
        
        // Doing Safety checks before starting 
        if (theta <= 0.0) {
            throw invalid_argument("theta must be strictly positive for mean reversion");
        }
        if (sigma_V <= 0.0) {
            throw invalid_argument("sigma_V must be strictly positive");
        }
        if (dt <= 0.0) {
            throw invalid_argument("dt must be strictly positive");
        }

        // storing decay factor: e^(-theta * dt) as step function called plenty of times with same fixed argument 
        decay_factor_ = exp(-theta * dt);

        double var_term = (1.0 - exp(-2.0 * theta * dt)) / (2.0 * theta);
        noise_standard_deviation_ = sigma_V * sqrt(var_term);

        // Stationary (long run equilibrium) variance of the OU process: Var[X∞] = sigma_V² / (2 * theta)
        stationary_variance_ = (sigma_V * sigma_V) / (2.0 * theta);
    }

    // long term average price : mu
    double get_stationary_mean() const {
        return mu_;
    }

    // long term variance (i.e) if the simulation ran to infinity
    double get_stationary_variance() const {
        return stationary_variance_;
    }

    // RNG generates random noise
    template<typename RNG>
    double step(double v_t, RNG& rng) {

        // Next Price = Baseline + (Current deviation from mean * Decay) + (Random Noise) as an exact solution of OU process
        // X(t+dt) = mu + (X(t)-mu)*e^(-theta*dt) + sigma_V * ∫[t,t+dt] e^(-theta(t+dt-s)) dW(s)
        // ∫[t,t+dt] e^(-theta(t+dt-s)) dW(s) is Gaussian with Mean = 0 and Var  = (1 - e^(-2*theta*dt)) / (2*theta)
       
        return mu_ + (v_t - mu_) * decay_factor_ + noise_standard_deviation_ * norm_dist_(rng);
    }

private:
    // baseline price
    double mu_;

    // fraction by which price decays back to mean
    double decay_factor_;
    
    double noise_standard_deviation_;

    double stationary_variance_;

    // N[0,1] sampler: gives numbers from -inf to +inf but most values cluster around 0(represnting std bell curve)
    normal_distribution<double> norm_dist_;
};

} 
