#pragma once

#include "arena_allocator.hpp"
#include "tsc_clock.hpp"
#include <cstdint>
#include <cstring>
#include <functional>
#include <cmath>

namespace arena {

// zero allocs used 
// use flat arrays by utilising ticks as indices so we don't have to binary search std::map like kiddos - O(logn) replaced by O(1)

struct alignas(32) Order {
    uint32_t id;
    int32_t price_ticks;    // price = ticks * tick_size
    int32_t quantity;       // Remaining quantity
    int32_t original_qty;   // Original quantity for fill reporting
    uint32_t timestamp_tsc; 
    int32_t next;           // Next order at same price level
    int32_t prev;           // Previous order at same price level
    bool is_buy;            // true = bid, false = ask
};

struct PriceLevel {
    int32_t head;           // First order index in pool (-1 = empty)
    int32_t tail;           // Last order index in pool (-1 = empty)
    int32_t total_qty;      // Total resting quantity at this level
    int32_t order_count;    // Number of orders at this level
};

struct Fill {
    uint32_t aggressive_id; // Incoming order
    uint32_t passive_id;    // Resting order
    int32_t price_ticks;    // Execution price
    int32_t quantity;       // Fill quantity
};

static constexpr int32_t LOB_MAX_LEVELS = 10000;  // Price range in ticks
static constexpr int32_t LOB_MAX_ORDERS = 65536;  // Order pool capacity
static constexpr int32_t LOB_MAX_FILLS  = 1024;   // Max fills per match call
static constexpr int32_t LOB_NULL       = -1;      // Null sentinel

class OrderBook {
public:
    
    OrderBook(double tick_size, double ref_price)
        : tick_size_(tick_size),
          ref_price_ticks_(LOB_MAX_LEVELS / 2),
          ref_price_(ref_price),
          next_order_id_(1),
          best_bid_(LOB_NULL),
          best_ask_(LOB_MAX_LEVELS),
          fill_count_(0) {

        std::memset(bid_levels_, 0, sizeof(bid_levels_));
        std::memset(ask_levels_, 0, sizeof(ask_levels_));
        for (int i = 0; i < LOB_MAX_LEVELS; ++i) {
            bid_levels_[i].head = LOB_NULL;
            bid_levels_[i].tail = LOB_NULL;
            ask_levels_[i].head = LOB_NULL;
            ask_levels_[i].tail = LOB_NULL;
        }
    }

    int32_t price_to_ticks(double price) const {
        return ref_price_ticks_ + static_cast<int32_t>(std::round((price - ref_price_) / tick_size_));
    }

    double ticks_to_price(int32_t ticks) const {
        return ref_price_ + (ticks - ref_price_ticks_) * tick_size_;
    }

    uint32_t add_order(bool is_buy, double price, int32_t quantity) {
        int32_t ticks = price_to_ticks(price);
        if (ticks < 0 || ticks >= LOB_MAX_LEVELS) return 0;

        int32_t idx = order_pool_.allocate_index();
        if (idx == LOB_NULL) return 0;

        Order* order = order_pool_.get(idx);
        order->id = next_order_id_++;
        order->price_ticks = ticks;
        order->quantity = quantity;
        order->original_qty = quantity;
        order->is_buy = is_buy;
        order->timestamp_tsc = static_cast<uint32_t>(TscClock::rdtscp());
        order->next = LOB_NULL;
        order->prev = LOB_NULL;

        PriceLevel* levels = is_buy ? bid_levels_ : ask_levels_;
        PriceLevel& level = levels[ticks];

        
        if (level.tail == LOB_NULL) {
            level.head = idx;
            level.tail = idx;
            // Since in case of Empty level: order is both head and tail

        } else {
            Order* tail_order = order_pool_.get(level.tail);
            tail_order->next = idx;
            order->prev = level.tail;
            level.tail = idx;
        }
        level.total_qty += quantity;
        level.order_count++;

        if (is_buy && ticks > best_bid_) {
            best_bid_ = ticks;
        }
        if (!is_buy && ticks < best_ask_) {
            best_ask_ = ticks;
        }

        return order->id;
    }

    // Returns the raw pool index instead of order ID
    
    int32_t add_order_pool_index(bool is_buy, double price, int32_t quantity) {
        int32_t ticks = price_to_ticks(price);
        if (ticks < 0 || ticks >= LOB_MAX_LEVELS) return LOB_NULL;

        int32_t idx = order_pool_.allocate_index();
        if (idx == LOB_NULL) return LOB_NULL;

        Order* order = order_pool_.get(idx);
        order->id = next_order_id_++;
        order->price_ticks = ticks;
        order->quantity = quantity;
        order->original_qty = quantity;
        order->is_buy = is_buy;
        order->timestamp_tsc = static_cast<uint32_t>(TscClock::rdtscp());
        order->next = LOB_NULL;
        order->prev = LOB_NULL;

        PriceLevel* levels = is_buy ? bid_levels_ : ask_levels_;
        PriceLevel& level = levels[ticks];

        if (level.tail == LOB_NULL) {
            level.head = idx;
            level.tail = idx;
        } else {
            Order* tail_order = order_pool_.get(level.tail);
            tail_order->next = idx;
            order->prev = level.tail;
            level.tail = idx;
        }
        level.total_qty += quantity;
        level.order_count++;

        if (is_buy && ticks > best_bid_) {
            best_bid_ = ticks;
        }
        if (!is_buy && ticks < best_ask_) {
            best_ask_ = ticks;
        }

        return idx;
    }

    bool cancel_order_by_index(int32_t idx) {
        if (idx < 0 || idx >= LOB_MAX_ORDERS) return false;

        Order* order = order_pool_.get(idx);
        PriceLevel* levels = order->is_buy ? bid_levels_ : ask_levels_;
        PriceLevel& level = levels[order->price_ticks];

        if (order->prev != LOB_NULL) {
            order_pool_.get(order->prev)->next = order->next;
        } else {
            level.head = order->next;
        }
        if (order->next != LOB_NULL) {
            order_pool_.get(order->next)->prev = order->prev;
        } else {
            level.tail = order->prev;
        }

        level.total_qty -= order->quantity;
        level.order_count--;

        // Update best bid/ask if we removed the best level that is get the next best value 
        
        if (order->is_buy && order->price_ticks == best_bid_ && level.order_count == 0) {
            while (best_bid_ >= 0 && bid_levels_[best_bid_].order_count == 0) {
                best_bid_--;
            }
        }
        
        /*
        This scan is technically O(k) where k is the gap to the next populated level. 
        In practice the gap is just 1 to 3 ticks for liquid books so it's effectively constant. 
        This is the only operation that isn't strictly O(1) worst case 
        */

        if (!order->is_buy && order->price_ticks == best_ask_ && level.order_count == 0) {
            while (best_ask_ < LOB_MAX_LEVELS && ask_levels_[best_ask_].order_count == 0) {
                best_ask_++;
            }
        }

        order_pool_.deallocate_index(idx);
        return true;
    }

    int32_t match_market_order(bool is_buy, int32_t quantity) {
        fill_count_ = 0;
        int32_t remaining = quantity;

        if (is_buy) {
            while (remaining > 0 && best_ask_ < LOB_MAX_LEVELS) {
                PriceLevel& level = ask_levels_[best_ask_];
                while (remaining > 0 && level.head != LOB_NULL) {
                    Order* resting = order_pool_.get(level.head);
                    int32_t fill_qty = std::min(remaining, resting->quantity);

                    if (fill_count_ < LOB_MAX_FILLS) {
                        fills_[fill_count_++] = {0, resting->id, best_ask_, fill_qty};
                    }

                    remaining -= fill_qty;
                    resting->quantity -= fill_qty;
                    level.total_qty -= fill_qty;

                    if (resting->quantity == 0) {

                        int32_t next = resting->next;
                        order_pool_.deallocate_index(level.head);
                        level.head = next;
                        if (next != LOB_NULL) {
                            order_pool_.get(next)->prev = LOB_NULL;
                        } else {
                            level.tail = LOB_NULL;
                        }
                        level.order_count--;
                    }
                }
                // Move to next ask level if current is exhausted
                if (level.order_count == 0) {
                    best_ask_++;
                }
            }
        } else {
            
            while (remaining > 0 && best_bid_ >= 0) {
                PriceLevel& level = bid_levels_[best_bid_];
                while (remaining > 0 && level.head != LOB_NULL) {
                    Order* resting = order_pool_.get(level.head);
                    int32_t fill_qty = std::min(remaining, resting->quantity);

                    if (fill_count_ < LOB_MAX_FILLS) {
                        fills_[fill_count_++] = {0, resting->id, best_bid_, fill_qty};
                    }

                    remaining -= fill_qty;
                    resting->quantity -= fill_qty;
                    level.total_qty -= fill_qty;

                    if (resting->quantity == 0) {
                        int32_t next = resting->next;
                        order_pool_.deallocate_index(level.head);
                        level.head = next;
                        if (next != LOB_NULL) {
                            order_pool_.get(next)->prev = LOB_NULL;
                        } else {
                            level.tail = LOB_NULL;
                        }
                        level.order_count--;
                    }
                }
                if (level.order_count == 0) {
                    best_bid_--;
                }
            }
        }

        return fill_count_;
    }

    // simulates liquidity provided by market maker, which helps in matching of orders in real exchanges

    void seed_liquidity(double mid_price, double half_spread,
                        int num_levels, int32_t qty_per_level) {
        for (int i = 0; i < num_levels; ++i) {
            double bid_price = mid_price - half_spread - i * tick_size_;
            double ask_price = mid_price + half_spread + i * tick_size_;
            add_order(true,  bid_price, qty_per_level);
            add_order(false, ask_price, qty_per_level);
        }
    }

    void clear() {
        for (int i = 0; i < LOB_MAX_LEVELS; ++i) {
            bid_levels_[i] = {LOB_NULL, LOB_NULL, 0, 0};
            ask_levels_[i] = {LOB_NULL, LOB_NULL, 0, 0};
        }
        best_bid_ = LOB_NULL;
        best_ask_ = LOB_MAX_LEVELS;
        fill_count_ = 0;
        next_order_id_ = 1;
        order_pool_.reset();
    }

    double get_best_bid_price() const {
        if (best_bid_ < 0) return 0.0;
        return ticks_to_price(best_bid_);
    }

    double get_best_ask_price() const {
        if (best_ask_ >= LOB_MAX_LEVELS) return 0.0;
        return ticks_to_price(best_ask_);
    }

    double get_spread() const {
        if (best_bid_ < 0 || best_ask_ >= LOB_MAX_LEVELS) return 0.0;
        return (best_ask_ - best_bid_) * tick_size_;
    }

    int32_t get_bid_depth(int32_t ticks) const {
        if (ticks < 0 || ticks >= LOB_MAX_LEVELS) return 0;
        return bid_levels_[ticks].total_qty;
    }

    int32_t get_ask_depth(int32_t ticks) const {
        if (ticks < 0 || ticks >= LOB_MAX_LEVELS) return 0;
        return ask_levels_[ticks].total_qty;
    }

    const Fill* get_fills() const { return fills_; }
    int32_t get_fill_count() const { return fill_count_; }

    size_t get_total_orders() const { return order_pool_.allocated(); }

private:
    double tick_size_;
    int32_t ref_price_ticks_;
    double ref_price_;
    uint32_t next_order_id_;
    
    int32_t best_bid_;  // Highest bid tick with orders
    int32_t best_ask_;  // Lowest ask tick with orders

    PriceLevel bid_levels_[LOB_MAX_LEVELS];
    PriceLevel ask_levels_[LOB_MAX_LEVELS];

    PoolAllocator<Order, LOB_MAX_ORDERS> order_pool_;

    Fill fills_[LOB_MAX_FILLS];
    int32_t fill_count_;
};

} // namespace arena
