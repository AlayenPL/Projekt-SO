#include <arpa/inet.h>
#include <csignal>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <memory>
#include <random>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "config.hpp"
#include "logger.hpp"
#include "park.hpp"
#include "tourist.hpp"

static int run_status_server(int port, std::atomic<int>& entered, std::atomic<int>& exited) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return 1;
    }
    if (listen(fd, 4) < 0) {
        perror("listen");
        close(fd);
        return 1;
    }

    while (true) {
        int c = accept(fd, nullptr, nullptr);
        if (c < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        std::string msg = "entered=" + std::to_string(entered.load()) +
                          " exited=" + std::to_string(exited.load()) + "\n";
        (void)!write(c, msg.data(), msg.size());
        close(c);
    }
    close(fd);
    return 0;
}

int main(int argc, char** argv) {
    Config cfg;
    try {
        cfg = Config::from_args(argc, argv);
        cfg.validate_or_throw();
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        return 1;
    }

    Logger log("logs/park.log");
    Park park(cfg, log);

    std::atomic<int> entered{0};
    std::atomic<int> exited{0};

    if (cfg.status_port > 0) {
        std::thread(run_status_server, cfg.status_port, std::ref(entered), std::ref(exited)).detach();
    }

    park.start();

    std::mt19937 rng(cfg.seed);
    std::uniform_int_distribution<int> age_dist(3, 70);
    std::bernoulli_distribution vip_dist(cfg.vip_prob);

    std::vector<std::unique_ptr<Tourist>> tourists;
    tourists.reserve(static_cast<size_t>(cfg.tourists_total));

    for (int i = 0; i < cfg.tourists_total; ++i) {
        int age = age_dist(rng);
        bool vip = vip_dist(rng);
        auto t = std::make_unique<Tourist>(i, age, vip, &park);
        t->start();
        tourists.push_back(std::move(t));
    }

    // Wait until all tourists have enqueued at the cashier, then close entry.
    while (park.enqueued.load() < cfg.tourists_total) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    park.close();

    for (auto& t : tourists) t->join();

    park.stop();

    entered.store(park.entered.load());
    exited.store(park.exited.load());

    std::cout << "[SUMMARY] tourists=" << cfg.tourists_total
              << " admitted=" << park.entered.load()
              << " exited=" << park.exited.load()
              << " log=logs/park.log\n";

    return 0;
}
