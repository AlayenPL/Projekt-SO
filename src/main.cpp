#include "config.hpp"
#include "logger.hpp"
#include "park.hpp"
#include "tourist.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    Config cfg = Config::from_args(argc, argv);

    try {
        Logger log("logs/sim.log");
        Park park(cfg, log);

        log.log_ts("MAIN", "START");
        park.start();

        // Spawn arriving tourists
        std::vector<std::unique_ptr<Tourist>> tourists;
        tourists.reserve(cfg.tourists_total);

        auto t0 = std::chrono::steady_clock::now();
        auto endt = t0 + std::chrono::milliseconds(cfg.duration_ms);

        for (int i = 0; i < cfg.tourists_total; ++i) {
            // Stop generating arrivals if park is closed
            if (std::chrono::steady_clock::now() >= endt) break;

            // Age distribution: 0-75
            int age = park.rand_int(0, 75);
            bool vip = (park.rand01() < 0.15);

            tourists.emplace_back(std::make_unique<Tourist>(i+1, age, vip, &park));
            tourists.back()->start();

            int delay = park.rand_int(0, cfg.arrival_jitter_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }

        // Let the park run until end time
        while (std::chrono::steady_clock::now() < endt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        park.open.store(false);
        park.entry_cv.notify_all();
        park.group_cv.notify_all();
        park.exit_cv.notify_all();

        // Join tourists
        for (auto& t : tourists) t->join();

        park.stop();
        log.log_ts("MAIN", "STOP");

        std::cout << "Simulation finished. Log: logs/sim.log\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
