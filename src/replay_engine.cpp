#include "replay_engine.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <random>
#include <chrono>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace arena {

MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(other.data_), size_(other.size_)
#ifdef _WIN32
    , file_handle_(other.file_handle_), map_handle_(other.map_handle_)
#else
    , fd_(other.fd_)
#endif
{
    other.data_ = nullptr;
    other.size_ = 0;
#ifdef _WIN32
    other.file_handle_ = nullptr;
    other.map_handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        close();
        data_ = other.data_; size_ = other.size_;
#ifdef _WIN32
        file_handle_ = other.file_handle_; map_handle_ = other.map_handle_;
        other.file_handle_ = nullptr; other.map_handle_ = nullptr;
#else
        fd_ = other.fd_; other.fd_ = -1;
#endif
        other.data_ = nullptr; other.size_ = 0;
    }
    return *this;
}

#ifdef _WIN32

bool MappedFile::open(const std::string& path) {
    close();

    file_handle_ = CreateFileA(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr
    );
    if (file_handle_ == INVALID_HANDLE_VALUE) {
        file_handle_ = nullptr;
        return false;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle_, &file_size)) {
        CloseHandle(file_handle_); file_handle_ = nullptr;
        return false;
    }
    size_ = static_cast<size_t>(file_size.QuadPart);

    if (size_ == 0) {
        CloseHandle(file_handle_); file_handle_ = nullptr;
        return false;
    }

    map_handle_ = CreateFileMappingA(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map_handle_) {
        CloseHandle(file_handle_); file_handle_ = nullptr;
        return false;
    }

    data_ = static_cast<uint8_t*>(MapViewOfFile(map_handle_, FILE_MAP_READ, 0, 0, 0));
    if (!data_) {
        CloseHandle(map_handle_); map_handle_ = nullptr;
        CloseHandle(file_handle_); file_handle_ = nullptr;
        size_ = 0;
        return false;
    }

    return true;
}

void MappedFile::close() {
    if (data_) { UnmapViewOfFile(data_); data_ = nullptr; }
    if (map_handle_) { CloseHandle(map_handle_); map_handle_ = nullptr; }
    if (file_handle_) { CloseHandle(file_handle_); file_handle_ = nullptr; }
    size_ = 0;
}

#else

bool MappedFile::open(const std::string& path) {
    close();

    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) return false;

    struct stat st;
    if (fstat(fd_, &st) < 0 || st.st_size == 0) {
        ::close(fd_); fd_ = -1;
        return false;
    }
    size_ = static_cast<size_t>(st.st_size);

    data_ = static_cast<uint8_t*>(mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0));
    if (data_ == MAP_FAILED) {
        data_ = nullptr; size_ = 0;
        ::close(fd_); fd_ = -1;
        return false;
    }

    madvise(data_, size_, MADV_SEQUENTIAL);

    return true;
}

void MappedFile::close() {
    if (data_) { munmap(data_, size_); data_ = nullptr; }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    size_ = 0;
}

#endif

LOBHandler::LOBHandler(OrderBook* book) : book_(book) {
    ref_to_index_.reserve(65536);
}

void LOBHandler::handle_add(uint64_t ref, bool is_buy, double price, int32_t shares, uint16_t locate) {
    if (stock_filter_ != 0 && locate != stock_filter_) return;

    uint32_t order_id = book_->add_order(is_buy, price, shares);
    if (order_id == 0) {
        orders_rejected_++;
        return;
    }

    ref_to_index_[ref] = static_cast<int32_t>(order_id - 1);
    orders_applied_++;
}

void LOBHandler::handle_cancel(uint64_t ref, int32_t /*shares*/) {
    auto it = ref_to_index_.find(ref);
    if (it == ref_to_index_.end()) return;

    book_->cancel_order_by_index(it->second);
    ref_to_index_.erase(it);
}

void LOBHandler::handle_delete(uint64_t ref) {
    auto it = ref_to_index_.find(ref);
    if (it == ref_to_index_.end()) return;

    book_->cancel_order_by_index(it->second);
    ref_to_index_.erase(it);
}

void LOBHandler::on_add_order(const itch::AddOrderMsg& msg) {
    handle_add(msg.get_order_ref(), msg.is_buy(), msg.get_price(),
               static_cast<int32_t>(msg.get_shares()), msg.get_locate());
}

void LOBHandler::on_add_order_mpid(const itch::AddOrderMPIDMsg& msg) {
    handle_add(msg.get_order_ref(), msg.is_buy(), msg.get_price(),
               static_cast<int32_t>(msg.get_shares()), msg.get_locate());
}

void LOBHandler::on_order_executed(const itch::OrderExecutedMsg& msg) {
    auto it = ref_to_index_.find(msg.get_order_ref());
    if (it == ref_to_index_.end()) return;
    book_->cancel_order_by_index(it->second);
    ref_to_index_.erase(it);
}

void LOBHandler::on_order_executed_price(const itch::OrderExecutedPriceMsg& msg) {
    auto it = ref_to_index_.find(msg.get_order_ref());
    if (it == ref_to_index_.end()) return;
    book_->cancel_order_by_index(it->second);
    ref_to_index_.erase(it);
}

void LOBHandler::on_order_cancel(const itch::OrderCancelMsg& msg) {
    handle_cancel(msg.get_order_ref(), static_cast<int32_t>(msg.get_cancelled_shares()));
}

void LOBHandler::on_order_delete(const itch::OrderDeleteMsg& msg) {
    handle_delete(msg.get_order_ref());
}

void LOBHandler::on_order_replace(const itch::OrderReplaceMsg& msg) {
    handle_delete(msg.get_original_ref());
    double mid = (book_->get_best_bid_price() + book_->get_best_ask_price()) / 2.0;
    bool is_buy = msg.get_price() < mid;
    handle_add(msg.get_new_ref(), is_buy, msg.get_price(),
               static_cast<int32_t>(msg.get_shares()), 0);
}

ReplayStats ReplayEngine::replay(const std::string& path, OrderBook& book, uint16_t locate) {
    ReplayStats stats;

    MappedFile file;
    if (!file.open(path)) {
        std::cerr << "[ReplayEngine] Failed to open: " << path << "\n";
        return stats;
    }

    std::cout << "[ReplayEngine] Mapped " << file.size() << " bytes ("
              << std::fixed << std::setprecision(1)
              << (file.size() / (1024.0 * 1024.0)) << " MB)\n";

    LOBHandler handler(&book);
    if (locate != 0) {
        handler.set_stock_filter(locate);
    }

    TscClock tsc;

    std::vector<double> latencies_ns;
    latencies_ns.reserve(1000000);

    auto wall_start = std::chrono::steady_clock::now();

    size_t offset = 0;
    const uint8_t* data = file.data();
    size_t len = file.size();

    while (offset + 2 < len) {
        uint16_t msg_len = itch::ntoh16(*reinterpret_cast<const uint16_t*>(data + offset));
        offset += 2;

        if (offset + msg_len > len || msg_len == 0) break;

        const uint8_t* msg_ptr = data + offset;
        char type = static_cast<char>(msg_ptr[0]);

        uint64_t t0 = TscClock::rdtscp();

        switch (type) {
            case 'A':
                handler.on_add_order(*reinterpret_cast<const itch::AddOrderMsg*>(msg_ptr));
                stats.parse_stats.add_orders++;
                break;
            case 'F':
                handler.on_add_order_mpid(*reinterpret_cast<const itch::AddOrderMPIDMsg*>(msg_ptr));
                stats.parse_stats.add_orders++;
                break;
            case 'E':
                handler.on_order_executed(*reinterpret_cast<const itch::OrderExecutedMsg*>(msg_ptr));
                stats.parse_stats.executions++;
                break;
            case 'C':
                handler.on_order_executed_price(*reinterpret_cast<const itch::OrderExecutedPriceMsg*>(msg_ptr));
                stats.parse_stats.executions++;
                break;
            case 'X':
                handler.on_order_cancel(*reinterpret_cast<const itch::OrderCancelMsg*>(msg_ptr));
                stats.parse_stats.cancels++;
                break;
            case 'D':
                handler.on_order_delete(*reinterpret_cast<const itch::OrderDeleteMsg*>(msg_ptr));
                stats.parse_stats.deletes++;
                break;
            case 'U':
                handler.on_order_replace(*reinterpret_cast<const itch::OrderReplaceMsg*>(msg_ptr));
                stats.parse_stats.replaces++;
                break;
            case 'P':
                handler.on_trade(*reinterpret_cast<const itch::TradeMsg*>(msg_ptr));
                stats.parse_stats.trades++;
                break;
            case 'S':
                handler.on_system_event(*reinterpret_cast<const itch::SystemEventMsg*>(msg_ptr));
                break;
            default:
                stats.parse_stats.unknown_types++;
                break;
        }

        uint64_t t1 = TscClock::rdtscp();
        latencies_ns.push_back(tsc.ticks_to_ns(t1 - t0));

        stats.parse_stats.messages_parsed++;
        offset += msg_len;
    }

    auto wall_end = std::chrono::steady_clock::now();

    stats.parse_stats.bytes_consumed = offset;

    stats.total_elapsed_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    if (stats.total_elapsed_ms > 0) {
        stats.msgs_per_second = stats.parse_stats.messages_parsed / (stats.total_elapsed_ms / 1000.0);
        stats.bytes_per_second = stats.parse_stats.bytes_consumed / (stats.total_elapsed_ms / 1000.0);
    }

    if (!latencies_ns.empty()) {
        std::sort(latencies_ns.begin(), latencies_ns.end());
        size_t n = latencies_ns.size();
        stats.p50_ns  = latencies_ns[n * 50 / 100];
        stats.p90_ns  = latencies_ns[n * 90 / 100];
        stats.p99_ns  = latencies_ns[n * 99 / 100];
        stats.p999_ns = latencies_ns[n * 999 / 1000];
        stats.min_ns  = latencies_ns[0];
        stats.max_ns  = latencies_ns[n - 1];
    }

    stats.best_bid     = book.get_best_bid_price();
    stats.best_ask     = book.get_best_ask_price();
    stats.spread       = book.get_spread();
    stats.total_orders = book.get_total_orders();

    std::cout << "\n";
    std::cout << "---xxx---\n";
    std::cout << "  ITCH REPLAY COMPLETE\n";
    std::cout << "---xxx---\n";
    std::cout << "  Messages parsed:   " << stats.parse_stats.messages_parsed << "\n";
    std::cout << "    Add Orders:      " << stats.parse_stats.add_orders << "\n";
    std::cout << "    Executions:      " << stats.parse_stats.executions << "\n";
    std::cout << "    Cancels:         " << stats.parse_stats.cancels << "\n";
    std::cout << "    Deletes:         " << stats.parse_stats.deletes << "\n";
    std::cout << "    Replaces:        " << stats.parse_stats.replaces << "\n";
    std::cout << "    Trades:          " << stats.parse_stats.trades << "\n";
    std::cout << "    Unknown:         " << stats.parse_stats.unknown_types << "\n";
    std::cout << "  Applied to LOB:    " << handler.orders_applied() << "\n";
    std::cout << "  Rejected (OOB):    " << handler.orders_rejected() << "\n";
    std::cout << "---xxxx---\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Elapsed:           " << stats.total_elapsed_ms << " ms\n";
    std::cout << "  Throughput:        " << (stats.msgs_per_second / 1e6) << " M msgs/sec\n";
    std::cout << "  Bandwidth:         " << (stats.bytes_per_second / (1024*1024)) << " MB/sec\n";
    std::cout << "---xxxx---\n";
    std::cout << "  Per-message latency (ns):\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "    p50:   " << stats.p50_ns << "\n";
    std::cout << "    p90:   " << stats.p90_ns << "\n";
    std::cout << "    p99:   " << stats.p99_ns << "\n";
    std::cout << "    p99.9: " << stats.p999_ns << "\n";
    std::cout << "    min:   " << stats.min_ns << "\n";
    std::cout << "    max:   " << stats.max_ns << "\n";
    std::cout << "---xxxx---\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  LOB State:\n";
    std::cout << "    Best Bid:  " << stats.best_bid << "\n";
    std::cout << "    Best Ask:  " << stats.best_ask << "\n";
    std::cout << "    Spread:    " << stats.spread << "\n";
    std::cout << "    Orders:    " << stats.total_orders << "\n";
    std::cout << "---xxx---\n";

    return stats;
}

bool ITCHGenerator::generate(const std::string& path, int num_msgs,
                              double ref_price, uint64_t seed) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> price_offset(-2.0, 2.0);
    std::uniform_int_distribution<int> qty_dist(1, 500);
    std::uniform_int_distribution<int> action_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);

    uint64_t next_ref = 1;
    uint64_t timestamp_ns = 34200000000000ULL;
    std::vector<uint64_t> live_orders;
    live_orders.reserve(num_msgs);

    auto write_msg = [&](const void* data, uint16_t msg_len) {
        uint16_t len_be = itch::ntoh16(msg_len);
        out.write(reinterpret_cast<const char*>(&len_be), 2);
        out.write(reinterpret_cast<const char*>(data), msg_len);
    };

    auto encode_timestamp = [](uint8_t* dst, uint64_t ns) {
        dst[0] = static_cast<uint8_t>((ns >> 40) & 0xFF);
        dst[1] = static_cast<uint8_t>((ns >> 32) & 0xFF);
        dst[2] = static_cast<uint8_t>((ns >> 24) & 0xFF);
        dst[3] = static_cast<uint8_t>((ns >> 16) & 0xFF);
        dst[4] = static_cast<uint8_t>((ns >> 8) & 0xFF);
        dst[5] = static_cast<uint8_t>(ns & 0xFF);
    };

    {
        itch::SystemEventMsg sys{};
        sys.type = 'S';
        sys.stock_locate = 0;
        sys.tracking_number = 0;
        encode_timestamp(sys.timestamp, timestamp_ns);
        sys.event_code = 'O';
        write_msg(&sys, 12);
    }

    for (int i = 0; i < num_msgs; ++i) {
        timestamp_ns += 1000 + (rng() % 50000);
        int action = action_dist(rng);

        if (action < 50 || live_orders.empty()) {
            itch::AddOrderMsg msg{};
            msg.type = 'A';
            msg.stock_locate = itch::ntoh16(1);
            msg.tracking_number = 0;
            encode_timestamp(msg.timestamp, timestamp_ns);
            msg.order_ref = itch::ntoh64(next_ref);
            msg.side = side_dist(rng) ? 'B' : 'S';
            msg.shares = itch::ntoh32(static_cast<uint32_t>(qty_dist(rng)));

            double price = ref_price + price_offset(rng);
            price = std::round(price * 100.0) / 100.0;
            msg.price = itch::ntoh32(static_cast<uint32_t>(price * 10000));

            std::memcpy(msg.stock, "ARENA   ", 8);
            write_msg(&msg, 36);

            live_orders.push_back(next_ref);
            next_ref++;

        } else if (action < 75 && !live_orders.empty()) {
            std::uniform_int_distribution<size_t> idx_dist(0, live_orders.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t ref = live_orders[idx];

            itch::OrderDeleteMsg msg{};
            msg.type = 'D';
            msg.stock_locate = itch::ntoh16(1);
            msg.tracking_number = 0;
            encode_timestamp(msg.timestamp, timestamp_ns);
            msg.order_ref = itch::ntoh64(ref);
            write_msg(&msg, 19);

            live_orders[idx] = live_orders.back();
            live_orders.pop_back();

        } else if (action < 90 && !live_orders.empty()) {
            std::uniform_int_distribution<size_t> idx_dist(0, live_orders.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t ref = live_orders[idx];

            itch::OrderExecutedMsg msg{};
            msg.type = 'E';
            msg.stock_locate = itch::ntoh16(1);
            msg.tracking_number = 0;
            encode_timestamp(msg.timestamp, timestamp_ns);
            msg.order_ref = itch::ntoh64(ref);
            msg.shares = itch::ntoh32(static_cast<uint32_t>(qty_dist(rng)));
            msg.match_number = itch::ntoh64(next_ref++);
            write_msg(&msg, 31);

            live_orders[idx] = live_orders.back();
            live_orders.pop_back();

        } else if (!live_orders.empty()) {
            std::uniform_int_distribution<size_t> idx_dist(0, live_orders.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t ref = live_orders[idx];

            itch::OrderCancelMsg msg{};
            msg.type = 'X';
            msg.stock_locate = itch::ntoh16(1);
            msg.tracking_number = 0;
            encode_timestamp(msg.timestamp, timestamp_ns);
            msg.order_ref = itch::ntoh64(ref);
            msg.cancelled_shares = itch::ntoh32(static_cast<uint32_t>(qty_dist(rng)));
            write_msg(&msg, 23);

            live_orders[idx] = live_orders.back();
            live_orders.pop_back();
        }
    }

    {
        timestamp_ns += 1000;
        itch::SystemEventMsg sys{};
        sys.type = 'S';
        sys.stock_locate = 0;
        sys.tracking_number = 0;
        encode_timestamp(sys.timestamp, timestamp_ns);
        sys.event_code = 'C';
        write_msg(&sys, 12);
    }

    out.close();

    std::ifstream check(path, std::ios::binary | std::ios::ate);
    auto file_size = check.tellg();
    std::cout << "[ITCHGenerator] Generated " << path << " ("
              << num_msgs << " messages, " << file_size << " bytes)\n";

    return true;
}

} // namespace arena