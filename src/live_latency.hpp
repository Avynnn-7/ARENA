#pragma once

#include "spsc_buffer.hpp"
#include <atomic>
#include <thread>
#include <string>
#include <cmath>

namespace arena {

class LiveLatency {
public:
    LiveLatency(const std::string& target_ip, int target_port, size_t buffer_capacity = 1024);
    ~LiveLatency();

    void start();
    
    void stop();

    double get_mu() const { return mu_.load(std::memory_order_acquire); }
    
    double get_sigma() const { return sigma_.load(std::memory_order_acquire); }

    // Real world latency mean and std dev derived from the log normal fit:  
    //  m = exp(mu + sigma^2 / 2) 
    //  s = m * sqrt(exp(sigma^2) - 1)

    double get_mean_latency() const {
        double mu = mu_.load(std::memory_order_acquire);
        double sig = sigma_.load(std::memory_order_acquire);
        return std::exp(mu + 0.5 * sig * sig);
    }

    double get_std_latency() const {
        double mu = mu_.load(std::memory_order_acquire);
        double sig = sigma_.load(std::memory_order_acquire);
        double m = std::exp(mu + 0.5 * sig * sig);
        return m * std::sqrt(std::expm1(sig * sig));
    }

    size_t get_sample_count() const { return sample_count_.load(std::memory_order_acquire); }

private:
    void echo_server_loop();
    void udp_measurement_loop();
    void mle_fitting_loop();
    
    double get_time_ns() const;

    std::string target_ip_;
    int target_port_;
    
    std::atomic<bool> running_;
    
    SPSCBuffer<double> rtt_buffer_;
    
    std::thread echo_thread_;
    std::thread udp_thread_;
    std::thread mle_thread_;
    
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

    alignas(64) std::atomic<double> mu_;
    alignas(64) std::atomic<double> sigma_;
    alignas(64) std::atomic<size_t> sample_count_;

#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

} // namespace arena
