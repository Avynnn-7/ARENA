#include <gtest/gtest.h>
#include "order_book.hpp"

using namespace arena;

class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        book = std::make_unique<OrderBook>(0.01, 100.0);
    }
    
    std::unique_ptr<OrderBook> book;
};

TEST_F(OrderBookTest, AddAndCancelOrder) {
    uint32_t order_id = book->add_order(true, 99.50, 100);
    EXPECT_GT(order_id, 0u);
    EXPECT_DOUBLE_EQ(book->get_best_bid_price(), 99.50);
    int32_t ticks = book->price_to_ticks(99.50);
    EXPECT_EQ(book->get_bid_depth(ticks), 100);
}

TEST_F(OrderBookTest, MatchMarketOrder) {
    book->add_order(true, 99.50, 100); 
    book->add_order(true, 99.40, 100); 
    
    book->add_order(false, 100.50, 100); 
    book->add_order(false, 100.60, 100); 
    
    EXPECT_DOUBLE_EQ(book->get_best_bid_price(), 99.50);
    EXPECT_DOUBLE_EQ(book->get_best_ask_price(), 100.50);
    EXPECT_DOUBLE_EQ(book->get_spread(), 100.50 - 99.50);
    
    int32_t fills = book->match_market_order(false, 50);
    EXPECT_EQ(fills, 1);
    
    const Fill* fill_data = book->get_fills();
    EXPECT_EQ(fill_data[0].quantity, 50);
    EXPECT_EQ(fill_data[0].price_ticks, book->price_to_ticks(99.50));
    
    int32_t ticks_9950 = book->price_to_ticks(99.50);
    EXPECT_EQ(book->get_bid_depth(ticks_9950), 50);
    
    fills = book->match_market_order(false, 100);
    EXPECT_EQ(fills, 2); 
    
    EXPECT_NEAR(book->get_best_bid_price(), 99.40, 1e-4);
    int32_t ticks_9940 = book->price_to_ticks(99.40);
    EXPECT_EQ(book->get_bid_depth(ticks_9940), 50);
}

TEST_F(OrderBookTest, ClearBook) {
    book->add_order(true, 99.50, 100);
    book->add_order(false, 100.50, 100);
    
    book->clear();
    
    EXPECT_DOUBLE_EQ(book->get_best_bid_price(), 0.0);
    EXPECT_DOUBLE_EQ(book->get_best_ask_price(), 0.0);
    EXPECT_EQ(book->get_total_orders(), 0u);
}
