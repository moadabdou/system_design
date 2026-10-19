#include "store.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

std::size_t process_memory(const std::string &field) {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with(field + ":")) {
            return std::stoull(line.substr(line.find(':') + 1)) * 1024;
        }
    }
    throw std::runtime_error("process memory unavailable");
}

int main(int argc, char **argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("usage: memory-pressure POLICY uniform|skewed|burst|large");
        }
        const std::string policy = argv[1], workload = argv[2];
        if ((policy != "noeviction" && policy != "allkeys-lru") ||
            (workload != "uniform" && workload != "skewed" && workload != "burst" &&
             workload != "large")) {
            throw std::runtime_error("invalid policy or workload");
        }
        constexpr std::size_t limit = 256 * 1024;
        constexpr std::size_t commands = 50000;
        const auto now = study::Store::Clock::time_point{};
        study::Store store(limit, policy == "noeviction" ? study::MemoryPolicy::NoEviction
                                                         : study::MemoryPolicy::AllKeysLru);
        std::vector<std::string> keys;
        for (int i = 0; i < 2000; ++i) {
            const auto text = std::to_string(i);
            keys.push_back(std::string(32 - text.size(), '0') + text);
        }
        // Allocate/touch measurement storage before sampling empty RSS.
        std::vector<double> latencies(commands, 0.0);
        const std::string value(1024, 'v'), large_value(64 * 1024, 'v'),
            huge_value(192 * 1024, 'v');
        const auto empty_rss = process_memory("VmRSS");
        const auto ttl =
            workload == "burst" ? std::chrono::milliseconds{10} : std::chrono::milliseconds{1000};
        for (std::size_t i = 0; i < keys.size(); ++i) {
            try {
                store.set(keys[i], value,
                          i % 2 || workload == "burst" ? std::optional{ttl} : std::nullopt, now);
            } catch (const study::MemoryError &) {
            }
        }
        const auto loaded_rss = process_memory("VmRSS");
        const auto before = store.stats();
        std::size_t accepted = 0, work_limit_errors = 0, peak_charge = store.used_bytes(),
                    maximum_work = 0;
        std::mt19937 random(20261019);
        const auto started = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < commands; ++i) {
            // Repeatable simulated monotonic time: 1 ms per 1,000 commands.
            const auto command_time =
                now + std::chrono::milliseconds{static_cast<long long>(i / 1000)};
            std::size_t index = random() % keys.size();
            if (workload == "skewed" && random() % 100 < 85) {
                index = random() % 64;
            }
            const auto operation = random() % 100;
            const auto request_start = std::chrono::steady_clock::now();
            if (operation < 90) {
                store.get(keys[index], command_time);
            } else if (operation < 99) {
                const auto &payload =
                    workload == "large" ? (i % 2 ? huge_value : large_value) : value;
                try {
                    store.set(keys[index], payload, index % 2 ? std::optional{ttl} : std::nullopt,
                              command_time);
                    ++accepted;
                } catch (const study::MemoryError &error) {
                    if (std::string(error.what()) == "eviction work limit reached") {
                        ++work_limit_errors;
                    }
                }
                maximum_work = std::max(maximum_work, store.stats().last_admission_work);
            } else {
                store.erase(keys[index], command_time);
            }
            latencies[i] = std::chrono::duration<double, std::micro>(
                               std::chrono::steady_clock::now() - request_start)
                               .count();
            peak_charge = std::max(peak_charge, store.used_bytes());
            if (store.used_bytes() > limit || store.recency_size() != store.size() ||
                store.expiring_size() > store.size() || maximum_work > 128) {
                throw std::runtime_error("memory/work/metadata invariant failed");
            }
            if (i % 32 == 31) {
                store.expire_due(128, command_time);
            }
        }
        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const auto after = store.stats();
        const auto hits = after.hits - before.hits, misses = after.misses - before.misses;
        std::sort(latencies.begin(), latencies.end());
        std::cout << "{\"policy\":\"" << policy << "\",\"workload\":\"" << workload
                  << "\",\"seed\":20261019,\"commands\":" << commands
                  << ",\"memory_limit\":" << limit
                  << ",\"key_bytes\":32,\"normal_value_bytes\":1024"
                  << ",\"ttl_ms\":" << ttl.count() << ",\"simulated_ms_per_1000_commands\":1"
                  << ",\"initial_rejected_sets\":" << before.rejected_writes
                  << ",\"accepted_sets\":" << accepted
                  << ",\"rejected_sets\":" << after.rejected_writes - before.rejected_writes
                  << ",\"work_limit_errors\":" << work_limit_errors
                  << ",\"evictions\":" << after.evictions - before.evictions
                  << ",\"expirations\":" << after.expirations - before.expirations
                  << ",\"get_hits\":" << hits << ",\"get_misses\":" << misses << ",\"hit_rate\":"
                  << static_cast<double>(hits) / static_cast<double>(hits + misses)
                  << ",\"commands_per_second\":" << static_cast<double>(commands) / elapsed
                  << ",\"operation_p50_us\":" << latencies[commands / 2 - 1]
                  << ",\"operation_p99_us\":" << latencies[commands * 99 / 100 - 1]
                  << ",\"peak_accounted_bytes\":" << peak_charge
                  << ",\"final_accounted_bytes\":" << store.used_bytes()
                  << ",\"empty_harness_rss_bytes\":" << empty_rss
                  << ",\"loaded_harness_rss_bytes\":" << loaded_rss
                  << ",\"final_harness_rss_bytes\":" << process_memory("VmRSS")
                  << ",\"peak_harness_rss_bytes\":" << process_memory("VmHWM")
                  << ",\"maximum_admission_work\":" << maximum_work << "}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
