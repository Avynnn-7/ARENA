#include "ou_sampler.hpp"
#include "single_agent.hpp"
#include "race_resolver.hpp"
#include "live_latency.hpp"
#include "math_utils.hpp"
#include "arena_allocator.hpp"
#include "tsc_clock.hpp"
#include "order_book.hpp"
#include <iostream>
#include <vector>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>
#include <numeric>
#include <memory>

void run_simulation(double mean_A, double std_A, double mean_B, double std_B,
                    const std::string& scenario_name) {
    double theta = 2.0;
    double mu = 0.0;
    double sigma_V = 1.0;
    double dt = 0.01;
    int steps = 1000;
    int num_paths = 50000;
    constexpr double half_spread = 0.05; 
    double cost_c = half_spread;
    
    
    arena::OUSampler sampler(theta, mu, sigma_V, dt);
    arena::SingleAgent agent_solo(mean_A, std_A, dt);
    arena::SingleAgent agent_a(mean_A, std_A, dt);
    arena::SingleAgent agent_b(mean_B, std_B, dt);
    arena::RaceResolver resolver;
    std::mt19937_64 rng_ou(42);
    std::mt19937_64 rng_solo(101);  
    std::mt19937_64 rng_a(201);     
    std::mt19937_64 rng_b(301);     
    
    double b_solo = arena::compute_solo_boundary(mean_A, std_A, theta, mu, cost_c);
    double b_a = arena::compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c); 
    double b_b = arena::compute_equilibrium_boundary(mean_B, std_B, mean_A, std_A, theta, mu, cost_c); 
    
    double pnl_solo = 0.0;
    double pnl_a = 0.0;
    double pnl_b = 0.0;
    int trades_solo = 0;
    int trades_a = 0;
    int trades_b = 0;
    
    // Per path PnL tracking -> Sharpe ratio computation
    std::vector<double> pnl_per_path_solo(num_paths, 0.0);
    std::vector<double> pnl_per_path_a(num_paths, 0.0);
    
    std::vector<int> stop_time_solo;
    std::vector<int> stop_time_a;
    stop_time_solo.reserve(num_paths);
    stop_time_a.reserve(num_paths);
    
    arena::ArenaAllocator arena(steps * sizeof(double) + 64); 
    
    auto lob = std::make_unique<arena::OrderBook>(0.01, mu);
    constexpr int lob_depth = 10;        // 10 levels each side
    constexpr int32_t qty_per_level = 100;
    double total_slippage = 0.0;
    int lob_fills = 0;
    
   
    arena::TscClock tsc;
    auto t_start = std::chrono::steady_clock::now();
    uint64_t tsc_start = arena::TscClock::rdtscp();
    
    for (int p = 0; p < num_paths; ++p) {
        arena.reset();
        double* v_history = arena.allocate<double>(steps);
        v_history[0] = mu;
        for (int i = 1; i < steps; ++i) {
            v_history[i] = sampler.step(v_history[i - 1], rng_ou);
        }

        // Fair value at a fractional (continuous) execution time t = i + delta/dt.
        auto v_at = [&](double t) -> double {
            if (t <= 0.0) return v_history[0];
            if (t >= static_cast<double>(steps - 1)) return v_history[steps - 1];
            int lo = static_cast<int>(t);
            double f = t - lo;
            return v_history[lo] * (1.0 - f) + v_history[lo + 1] * f;
        };
        
        bool solo_acted = false;
        bool game_resolved = false;
        int mm_update_freq = 5; 
        
        for (int i = 1; i < steps; ++i) {

            // Market Maker quotes a STATIC book anchored at the unconditional mean mu.
            // For a mean reverting fundamental the rational static quote is mu +/-
            // half_spread; the transient deviation (V_i - mu) is the stale edge a
            // latency winner snipes before the MM can reprice

            if (i % mm_update_freq == 0) {
                lob->clear();
                lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
            }

            if (!solo_acted) {
                auto decision_solo = agent_solo.evaluate_action(v_history, steps, i, b_solo, rng_solo);
                if (decision_solo.wants_to_act) {
                    double exec_t = i + decision_solo.latency_drawn / dt;
                    double v_exec = v_at(exec_t);
                    lob->clear();
                    lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                    double best_ask_before = lob->get_best_ask_price();
                    lob->match_market_order(true, 1); 
                    const arena::Fill* fills = lob->get_fills();
                    double fill_price = (lob->get_fill_count() > 0)
                        ? lob->ticks_to_price(fills[0].price_ticks)
                        : best_ask_before;
                    double slippage = fill_price - best_ask_before;
                    total_slippage += slippage;
                    lob_fills++;

                    double path_pnl = v_exec - fill_price;
                    pnl_solo += path_pnl;
                    pnl_per_path_solo[p] = path_pnl;
                    trades_solo++;
                    stop_time_solo.push_back(i);
                    solo_acted = true;
                }
            }
            
            if (!game_resolved) {
                auto dec_a = agent_a.evaluate_action(v_history, steps, i, b_a, rng_a);
                auto dec_b = agent_b.evaluate_action(v_history, steps, i, b_b, rng_b);
                
                if (dec_a.wants_to_act && dec_b.wants_to_act) {
                    // CONTESTED RACE -> Winner snipes the stale mu quote; loser arrives
                    // after the MM has repriced to the corrected fair, so it crosses a
                    // fresh spread and nets exactly -half_spread
                    auto result = resolver.resolve_race(dec_a.latency_drawn, dec_b.latency_drawn);
                    double exec_t_a = i + dec_a.latency_drawn / dt;
                    double exec_t_b = i + dec_b.latency_drawn / dt;
                    double v_exec_a = v_at(exec_t_a);
                    double v_exec_b = v_at(exec_t_b);

                    if (result.agent_a_won) {
                        lob->clear();
                        lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                        lob->match_market_order(true, 1);
                        const arena::Fill* fills = lob->get_fills();
                        double fill_price = (lob->get_fill_count() > 0)
                            ? lob->ticks_to_price(fills[0].price_ticks) : (mu + half_spread);
                        pnl_a += (v_exec_a - fill_price);
                        pnl_per_path_a[p] = v_exec_a - fill_price;
                        trades_a++;
                        stop_time_a.push_back(i);

                        lob->clear();
                        lob->seed_liquidity(v_exec_b, half_spread, lob_depth, qty_per_level);
                        lob->match_market_order(true, 1);
                        const arena::Fill* lfills = lob->get_fills();
                        double lfill = (lob->get_fill_count() > 0)
                            ? lob->ticks_to_price(lfills[0].price_ticks) : (v_exec_b + half_spread);
                        pnl_b += (v_exec_b - lfill); // = -half_spread
                        trades_b++;
                    } else if (result.agent_b_won) {
                        lob->clear();
                        lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                        lob->match_market_order(true, 1);
                        const arena::Fill* fills = lob->get_fills();
                        double fill_price = (lob->get_fill_count() > 0)
                            ? lob->ticks_to_price(fills[0].price_ticks) : (mu + half_spread);
                        pnl_b += (v_exec_b - fill_price);
                        trades_b++;
 
                        lob->clear();
                        lob->seed_liquidity(v_exec_a, half_spread, lob_depth, qty_per_level);
                        lob->match_market_order(true, 1);
                        const arena::Fill* lfills = lob->get_fills();
                        double lfill = (lob->get_fill_count() > 0)
                            ? lob->ticks_to_price(lfills[0].price_ticks) : (v_exec_a + half_spread);
                        double la_pnl = v_exec_a - lfill; // = -half_spread
                        pnl_a += la_pnl;
                        pnl_per_path_a[p] = la_pnl;
                        trades_a++;
                        stop_time_a.push_back(i);
                    }
                    game_resolved = true;
                } else if (dec_a.wants_to_act) {
                    //Uncontested
                    double v_exec = v_at(i + dec_a.latency_drawn / dt);
                    lob->clear();
                    lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                    lob->match_market_order(true, 1);
                    const arena::Fill* fills = lob->get_fills();
                    double fill_price = (lob->get_fill_count() > 0)
                        ? lob->ticks_to_price(fills[0].price_ticks) : (mu + half_spread);
                    double path_pnl = v_exec - fill_price;
                    pnl_a += path_pnl;
                    pnl_per_path_a[p] = path_pnl;
                    trades_a++;
                    stop_time_a.push_back(i);
                    game_resolved = true;
                } else if (dec_b.wants_to_act) {
                    double v_exec = v_at(i + dec_b.latency_drawn / dt);
                    lob->clear();
                    lob->seed_liquidity(mu, half_spread, lob_depth, qty_per_level);
                    lob->match_market_order(true, 1);
                    const arena::Fill* fills = lob->get_fills();
                    double fill_price = (lob->get_fill_count() > 0)
                        ? lob->ticks_to_price(fills[0].price_ticks) : (mu + half_spread);
                    pnl_b += (v_exec - fill_price);
                    trades_b++;
                    game_resolved = true;
                }
            }
        }
    }
    
    auto t_end = std::chrono::steady_clock::now();
    uint64_t tsc_end = arena::TscClock::rdtscp();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    double elapsed_ms_tsc = tsc.ticks_to_ns(tsc_end - tsc_start) / 1e6;
    double paths_per_sec = num_paths / (elapsed_ms / 1000.0);
    
    // stats
    double win_rate_a = (trades_a + trades_b > 0) ? static_cast<double>(trades_a) / (trades_a + trades_b) : 0.0;
    double win_rate_b = (trades_a + trades_b > 0) ? static_cast<double>(trades_b) / (trades_a + trades_b) : 0.0;
    double avg_pnl_solo = trades_solo ? pnl_solo / num_paths : 0.0;
    double avg_pnl_a = trades_a ? pnl_a / num_paths : 0.0;
    
    double p_win_a = arena::compute_p_win(mean_A, std_A, mean_B, std_B);
    double sharpe_solo = arena::compute_sharpe_ratio(pnl_per_path_solo.data(), num_paths);
    double sharpe_a = arena::compute_sharpe_ratio(pnl_per_path_a.data(), num_paths);
    
    // Mean stopping times
    double mean_stop_solo = 0.0;
    if (!stop_time_solo.empty()) {
        mean_stop_solo = std::accumulate(stop_time_solo.begin(), stop_time_solo.end(), 0.0) 
                         / stop_time_solo.size();
    }
    double mean_stop_a = 0.0;
    if (!stop_time_a.empty()) {
        mean_stop_a = std::accumulate(stop_time_a.begin(), stop_time_a.end(), 0.0) 
                      / stop_time_a.size();
    }
    
    // Lag 1 autocorrelation of OU path -> generate a dedicated validation path
    arena.reset();
    double* validation_path = arena.allocate<double>(steps);
    validation_path[0] = mu;
    std::mt19937_64 rng_validation(999);
    for (int i = 1; i < steps; ++i) {
        validation_path[i] = sampler.step(validation_path[i - 1], rng_validation);
    }
    double empirical_rho1 = arena::compute_lag1_autocorrelation(validation_path, steps);
    double theoretical_rho1 = std::exp(-theta * dt);
    
    std::cout << "\nARENA Simulation: " << scenario_name << std::endl;
    std::cout << "Agent A latency: mean=" << mean_A << ", std=" << std_A
              << "  |  Agent B latency: mean=" << mean_B << ", std=" << std_B << std::endl;
    std::cout << "Equilibrium b_A* = " << b_a << " | b_B* = " << b_b 
              << " | P(A wins race) = " << p_win_a << std::endl;
    std::cout << "------xxxxxxxx-------" << std::endl;
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Agent Solo | Trades: " << trades_solo << " | Avg PnL: " << avg_pnl_solo
              << " | Sharpe: " << sharpe_solo << " | Mean Stop: " << mean_stop_solo << std::endl;
    std::cout << "Agent A    | Trades: " << trades_a << " | Win Rate: " << win_rate_a * 100.0 
              << "% | Avg PnL: " << avg_pnl_a << " | Sharpe: " << sharpe_a 
              << " | Mean Stop: " << mean_stop_a << std::endl;
    std::cout << "Agent B    | Trades: " << trades_b << " | Win Rate: " << win_rate_b * 100.0 << "%" << std::endl;
    
    if (p_win_a < 0.5) {
        std::cout << "-> Competitive Cost (Solo - Agent A PnL): " << (avg_pnl_solo - avg_pnl_a) << std::endl;
    }
    
    std::cout << "------xxxxxxxx-------" << std::endl;
    std::cout << "OU Path Validation: empirical rho_1=" << empirical_rho1 
              << " | theoretical exp(-theta*dt)=" << theoretical_rho1 << std::endl;
    std::cout << "Performance: " << num_paths << " paths in " << elapsed_ms << " ms ("
              << static_cast<int>(paths_per_sec) << " paths/sec)" << std::endl;
    std::cout << "TSC Timing:  " << elapsed_ms_tsc << " ms | TSC freq: "
              << tsc.get_estimated_freq_ghz() << " GHz" << std::endl;
    double avg_slippage = lob_fills > 0 ? total_slippage / lob_fills : 0.0;
    std::cout << "LOB Engine:  " << lob_fills << " fills | Avg Slippage: " 
              << avg_slippage << " | Spread: " << (2.0 * half_spread) 
              << " | Depth: " << lob_depth << " levels" << std::endl;
}

int main() {
    std::cout << "Initializing Live UDP Latency Measurement..." << std::endl;
    arena::LiveLatency live_latency("127.0.0.1", 12345);
    live_latency.start();
    
    constexpr size_t min_warmup_samples = 100;
    std::cout << "Warming up: waiting for " << min_warmup_samples << " RTT samples..." << std::endl;
    
    int warmup_attempts = 0;
    while (live_latency.get_sample_count() < min_warmup_samples) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        warmup_attempts++;
        if (warmup_attempts > 100) { 
            std::cout << "WARNING: Only " << live_latency.get_sample_count() 
                      << " samples after 10s. Proceeding with available data." << std::endl;
            std::cout << "(Is the UDP echo server running? python tests/udp_echo_server.py)" << std::endl;
            break;
        }
    }
    std::cout << "Collected " << live_latency.get_sample_count() << " warmup samples." << std::endl;
    
    const double model_mean_latency = 0.02; 
    const double mean_B = 0.02;             
    const double std_B = 0.004;             
    
    for (int iter = 1; iter <= 5; ++iter) {
        std::cout << "\n=== Live Update Tick " << iter << " ===" << std::endl;
        
        double live_mean = live_latency.get_mean_latency();
        double live_std = live_latency.get_std_latency();
        double cv_live = (live_mean > 0.0) ? live_std / live_mean : 0.0;
        
        std::cout << "Fitted Live Latency: mean=" << live_mean << ", std=" << live_std 
                  << ", CV=" << cv_live << " (n=" << live_latency.get_sample_count() << ")" << std::endl;
        
        if (std::isnan(cv_live) || cv_live <= 0.0) {
            std::cout << "Insufficient samples for dispersion estimate. Skipping." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        if (cv_live < 0.01) cv_live = 0.01;
        if (cv_live > 2.0) cv_live = 2.0;
        
        double mean_A = model_mean_latency;
        double std_A = model_mean_latency * cv_live; 
        
        run_simulation(mean_A, std_A, mean_B, std_B, "Live Dynamic Game Engine");
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    live_latency.stop();
    return 0;
}
