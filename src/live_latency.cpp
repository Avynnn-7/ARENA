#include "live_latency.hpp"
#include "tsc_clock.hpp"
#include <cmath>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <intrin.h>
#else
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <x86intrin.h>
#endif

namespace arena {

LiveLatency::LiveLatency(const std::string& target_ip, int target_port, size_t buffer_capacity)
    : target_ip_(target_ip), target_port_(target_port), running_(false),
      rtt_buffer_(buffer_capacity), mu_(-4.0), sigma_(0.5), sample_count_(0) {
}

LiveLatency::~LiveLatency() {
    stop();
}

void LiveLatency::start() {
    if (running_.exchange(true)) return;
    
    echo_thread_ = std::thread(&LiveLatency::echo_server_loop, this);
    udp_thread_ = std::thread(&LiveLatency::udp_measurement_loop, this);
    mle_thread_ = std::thread(&LiveLatency::mle_fitting_loop, this);
}

void LiveLatency::stop() {
    if (!running_.exchange(false)) return;
    
    if (echo_thread_.joinable()) echo_thread_.join();
    if (udp_thread_.joinable()) udp_thread_.join();
    if (mle_thread_.joinable()) mle_thread_.join();
}

double LiveLatency::get_time_ns() const {
    unsigned int aux;
    return static_cast<double>(__rdtscp(&aux));
}

void LiveLatency::echo_server_loop() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Echo server: failed to create socket" << std::endl;
        WSACleanup();
        return;
    }
    DWORD timeout_ms = 100;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Echo server: failed to create socket" << std::endl;
        return;
    }
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    struct sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(static_cast<uint16_t>(target_port_));
    inet_pton(AF_INET, target_ip_.c_str(), &bind_addr.sin_addr);

    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        std::cerr << "Echo server: bind to port " << target_port_ << " failed (already in use?)" << std::endl;
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return;
    }

    char buf[64];
    struct sockaddr_in client_addr{};

    while (running_.load(std::memory_order_relaxed)) {
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        int bytes = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr*)&client_addr, &client_len);
        if (bytes > 0) {
            sendto(sock, buf, bytes, 0,
                   (struct sockaddr*)&client_addr, client_len);
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}

void LiveLatency::udp_measurement_loop() {
    // pinning to core 2 coz core 0 handles all the OS interrupt noise
#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), (1ULL << 2)); // Core 2
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset); 
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif

    TscClock tsc;
    double ticks_per_ns = tsc.get_ticks_per_ns();

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) return;
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;
#endif

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(target_port_));
    inet_pton(AF_INET, target_ip_.c_str(), &server_addr.sin_addr);

    char send_buf[64] = "ARENA_PING";
    char recv_buf[64];

#ifdef _WIN32

    // windows fallback dude - select() is too time consuming but it works here
    while (running_.load(std::memory_order_relaxed)) {
        uint64_t tsc_send = TscClock::rdtscp();

        sendto(sock, send_buf, sizeof(send_buf), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        struct timeval tv = {0, 5000}; 
        int sel = select(0, &read_fds, nullptr, nullptr, &tv);

        if (sel > 0) {
            int bytes = recv(sock, recv_buf, sizeof(recv_buf), 0);
            if (bytes > 0) {
                uint64_t tsc_recv = TscClock::rdtscp();
                double rtt_seconds = static_cast<double>(tsc_recv - tsc_send) 
                                     / (ticks_per_ns * 1e9);
                rtt_buffer_.push(rtt_seconds);
            }
        }
    }
    closesocket(sock);
    WSACleanup();

#else
    // Ofc using epoll coz it's far better than select 
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        close(sock);
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev);

    struct epoll_event events[1];

    while (running_.load(std::memory_order_relaxed)) {
        uint64_t tsc_send = TscClock::rdtscp();

        sendto(sock, send_buf, sizeof(send_buf), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));

        int nfds = epoll_wait(epfd, events, 1, 5);

        if (nfds > 0) {
            int bytes = recv(sock, recv_buf, sizeof(recv_buf), 0);
            if (bytes > 0) {
                uint64_t tsc_recv = TscClock::rdtscp();
                double rtt_seconds = static_cast<double>(tsc_recv - tsc_send)
                                     / (ticks_per_ns * 1e9);
                rtt_buffer_.push(rtt_seconds);
            }
        }
    }

    close(epfd);
    close(sock);
#endif
}

void LiveLatency::mle_fitting_loop() {
    
    // Using Wellford's algorithm yet again in the project for getting mean and variance 
    
    size_t count = 0;
    double mean_log = 0.0;
    double m2_log = 0.0;
    
    while (running_.load(std::memory_order_relaxed)) {
        double rtt_seconds = 0.0;
        
        while (rtt_buffer_.pop(rtt_seconds)) {
            if (rtt_seconds <= 0) continue;
            
            double log_rtt = std::log(rtt_seconds);
            count++;
            
            double delta = log_rtt - mean_log;
            mean_log += delta / static_cast<double>(count);
            double delta2 = log_rtt - mean_log;
            m2_log += delta * delta2;
            
            if (count > 1) {
                double var_log = m2_log / static_cast<double>(count - 1);
                double stddev_log = std::sqrt(var_log);
                
                mu_.store(mean_log, std::memory_order_release);
                sigma_.store(stddev_log, std::memory_order_release);
                sample_count_.store(count, std::memory_order_release);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace arena
