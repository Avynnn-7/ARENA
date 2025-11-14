#pragma once

/*
 * Maps an ITCH binary file into virtual memory and replays it through
 * the LOB at configurable speed. Measures per message processing latency
 * and reports overall statistics.
 */

#include "itch_parser.hpp"
#include "order_book.hpp"
#include "tsc_clock.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace arena {


class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    bool open(const std::string& path);
    void close();

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    bool is_open() const { return data_ != nullptr; }

private:
    uint8_t* data_ = nullptr;
    size_t size_ = 0;

#ifdef _WIN32
    void* file_handle_ = nullptr;  
    void* map_handle_  = nullptr;  
#else
    int fd_ = -1;
#endif
};

// Replaying of the Statistics =>

struct ReplayStats {
    itch::ParseStats parse_stats;

    // Timing
    double total_elapsed_ms   = 0.0;
    double msgs_per_second    = 0.0;
    double bytes_per_second   = 0.0;

    // LOB state at end of replay
    double best_bid           = 0.0;
    double best_ask           = 0.0;
    double spread             = 0.0;
    size_t total_orders       = 0;

    // Per message latency percentiles in nanosec
    double p50_ns = 0.0;
    double p90_ns = 0.0;
    double p99_ns = 0.0;
    double p999_ns = 0.0;
    double min_ns = 0.0;
    double max_ns = 0.0;
};


class LOBHandler : public itch::MessageHandler {
public:
    explicit LOBHandler(OrderBook* book);

    void on_add_order(const itch::AddOrderMsg& msg) override;
    void on_add_order_mpid(const itch::AddOrderMPIDMsg& msg) override;
    void on_order_executed(const itch::OrderExecutedMsg& msg) override;
    void on_order_executed_price(const itch::OrderExecutedPriceMsg& msg) override;
    void on_order_cancel(const itch::OrderCancelMsg& msg) override;
    void on_order_delete(const itch::OrderDeleteMsg& msg) override;
    void on_order_replace(const itch::OrderReplaceMsg& msg) override;

    void set_stock_filter(uint16_t locate) { stock_filter_ = locate; }

    size_t orders_applied() const { return orders_applied_; }
    size_t orders_rejected() const { return orders_rejected_; }

private:
    OrderBook* book_;
    uint16_t stock_filter_ = 0;
    size_t orders_applied_ = 0;
    size_t orders_rejected_ = 0;

    std::unordered_map<uint64_t, int32_t> ref_to_index_;

    void handle_add(uint64_t ref, bool is_buy, double price, int32_t shares, uint16_t locate);
    void handle_cancel(uint64_t ref, int32_t shares);
    void handle_delete(uint64_t ref);
};

// Replay an ITCH binary file through the given OrderBook.

class ReplayEngine {
public:
    static ReplayStats replay(const std::string& path, OrderBook& book, uint16_t locate = 0);
};

// Synthetic ITCH Data Generator - for testing without real exchange data

class ITCHGenerator {
public:
    static bool generate(const std::string& path, int num_msgs, 
                         double ref_price = 100.0, uint64_t seed = 42);
};

} // namespace arena
