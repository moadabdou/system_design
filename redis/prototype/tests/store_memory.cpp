#include "store.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using study::MemoryError;
using study::MemoryPolicy;
using study::Store;
using namespace std::chrono_literals;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Action> void rejected(Action action, const std::string &message) {
    try {
        action();
    } catch (const MemoryError &error) {
        require(error.what() == message, "wrong capacity error");
        return;
    }
    throw std::runtime_error("write should have been rejected");
}

void valid(const Store &store, std::size_t limit) {
    require(store.used_bytes() <= limit, "dataset exceeds limit");
    require(store.size() == store.recency_size(), "LRU node leak");
    require(store.expiring_size() <= store.size(), "timer leak");
    require(store.stats().last_admission_work <= 128, "admission work exceeded budget");
}

std::string key(int i) {
    auto text = std::to_string(i);
    return std::string(4 - text.size(), '0') + text;
}

int main() {
    try {
        const auto now = Store::Clock::time_point{};
        require(Store::entry_charge(0, 0, false) == 192, "empty entry accounting");
        require(Store::entry_charge(1, 1, true) == 292, "TTL accounting");
        rejected([] { Store::entry_charge(std::numeric_limits<std::size_t>::max(), 1, false); },
                 "entry exceeds memory limit");
        rejected([] { Store::entry_charge(1, std::numeric_limits<std::size_t>::max(), true); },
                 "entry exceeds memory limit");
        {
            Store store(600);
            store.set("a", "a", 10ms, now);                           // 292
            store.set("b", std::string(114, 'b'), std::nullopt, now); // 308
            require(store.used_bytes() == 600, "exact-fit SET failed");
            rejected([&] { store.set("a", "aa", 20ms, now); }, "memory limit reached");
            require(*store.get("a", now) == "a", "rejected replacement changed value");
            require(store.expire_due(128, now + 10ms) == 1, "rejected replacement changed TTL");
            require(store.used_bytes() == 308, "expiration accounting");
            store.set("b", std::string(114, 'c'), std::nullopt, now);
            store.set("b", "small", std::nullopt, now);
            require(store.used_bytes() == 199, "shrinking replacement accounting");
            require(store.erase("b", now) && store.used_bytes() == 0, "DEL accounting");
            valid(store, 600);
        }
        {
            Store store(292);
            store.set("a", "a", 10ms, now);
            store.set("a", "a", std::nullopt, now);
            require(store.used_bytes() == 195 && store.expiring_size() == 0, "TTL clearing charge");
            store.set("a", "a", 10ms, now);
            require(store.used_bytes() == 292, "TTL addition charge");
            require(!store.erase("a", now + 10ms) && store.used_bytes() == 0, "expired DEL charge");
            store.set("", "", std::nullopt, now);
            require(store.used_bytes() == 192, "empty key/value charge");
            store.erase("", now);
            const std::string binary("a\0", 2);
            store.set(binary, std::string("v\0", 2), std::nullopt, now);
            require(store.used_bytes() == 198, "binary charge");
            valid(store, 292);
        }
        for (const auto policy : {MemoryPolicy::NoEviction, MemoryPolicy::AllKeysLru}) {
            Store store(400, policy);
            store.set("a", "a", 10ms, now); // 292
            rejected([&] { store.set("a", std::string(401, 'x'), std::nullopt, now); },
                     "entry exceeds memory limit");
            require(*store.get("a", now) == "a", "oversized replacement mutated value");
            store.set("b", "b", std::nullopt, now + 10ms);
            require(store.stats().expirations == 1 && store.stats().evictions == 0,
                    "expired entry should be reclaimed before live eviction");
            require(store.used_bytes() == 195 && store.expiring_size() == 0, "reclaim accounting");
            valid(store, 400);
        }
        {
            Store store(3 * 195, MemoryPolicy::AllKeysLru);
            store.set("a", "a", std::nullopt, now);
            store.set("b", "b", std::nullopt, now);
            store.set("c", "c", std::nullopt, now);
            store.get("a", now); // order a,c,b
            store.get("missing", now);
            rejected([&] { store.set("b", std::string(600, 'x'), std::nullopt, now); },
                     "entry exceeds memory limit");
            store.set("d", "d", std::nullopt, now);
            require(store.get("b", now) == nullptr, "failed SET refreshed recency");
            require(store.get("a", now) && store.get("c", now) && store.get("d", now),
                    "wrong GET-refreshed victim");
            store.set("c", "c", std::nullopt, now); // c,d,a
            store.set("e", "e", std::nullopt, now);
            require(store.get("a", now) == nullptr, "successful SET did not refresh recency");
            require(store.stats().evictions == 2, "eviction counter wrong");
            valid(store, 585);
        }
        {
            Store store(600, MemoryPolicy::AllKeysLru);
            store.set("a", "a", std::nullopt, now); // oldest, replacement target
            store.set("b", "b", std::nullopt, now);
            store.set("c", "c", std::nullopt, now);
            store.set("a", std::string(220, 'a'), std::nullopt, now); // 414, needs two victims
            require(store.size() == 1 && store.stats().evictions == 2,
                    "multiple victims/target exclusion");
            require(store.get("a", now)->size() == 220, "replacement lost");
            valid(store, 600);
        }
        {
            Store store(600, MemoryPolicy::AllKeysLru);
            store.set("a", "a", 1h, now);                             // live TTL victim: 292
            store.set("b", std::string(114, 'b'), std::nullopt, now); // 308
            store.set("c", "c", std::nullopt, now);
            require(store.expiring_size() == 0 && store.get("a", now) == nullptr,
                    "TTL victim retained timer");
            require(store.used_bytes() == 503, "eviction charge wrong");
            valid(store, 600);
        }
        {
            Store store(40000, MemoryPolicy::AllKeysLru);
            for (int i = 0; i < 200; ++i) {
                store.set(key(i), "", std::nullopt, now); // 200 bytes each
            }
            rejected([&] { store.set("0000", std::string(25800, 'x'), std::nullopt, now); },
                     "eviction work limit reached"); // would require 129 victims
            require(store.size() == 200 && store.used_bytes() == 40000 &&
                        store.stats().evictions == 0,
                    "failed selection partially evicted live keys");
            require(store.stats().last_admission_work == 128, "selection budget wrong");
            store.set("next", "", std::nullopt, now);
            require(store.get("0000", now) == nullptr, "failed replacement changed recency");
            store.set("huge", std::string(25400, 'x'), std::nullopt, now); // exactly 128 victims
            require(store.stats().last_admission_work == 128 && store.stats().evictions == 129,
                    "exact-budget eviction should succeed");
            valid(store, 40000);
        }
        {
            Store store(41000, MemoryPolicy::AllKeysLru);
            for (int i = 0; i < 200; ++i) {
                store.set(key(i), "", i < 10 ? std::optional{10ms} : std::nullopt, now);
            }
            rejected([&] { store.set("next", std::string(26600, 'x'), std::nullopt, now + 10ms); },
                     "eviction work limit reached");
            require(store.stats().last_admission_work == 128 && store.stats().expirations == 10 &&
                        store.stats().evictions == 0 && store.size() == 190 &&
                        store.used_bytes() == 38000,
                    "expiration and LRU must share the admission budget");
            store.set("next", std::string(26600, 'x'), std::nullopt, now + 10ms);
            require(store.stats().last_admission_work == 119 && store.stats().evictions == 119,
                    "admission work did not reset for next SET");
            valid(store, 41000);
        }
        for (const auto policy : {MemoryPolicy::NoEviction, MemoryPolicy::AllKeysLru}) {
            Store store(200 * 300, policy);
            for (int i = 0; i < 200; ++i) {
                store.set(key(i), "", 10ms, now); // 300 bytes each
            }
            rejected([&] { store.set("next", std::string(50000, 'x'), std::nullopt, now + 10ms); },
                     "eviction work limit reached");
            require(store.size() == 72 && store.expiring_size() == 72 &&
                        store.stats().last_admission_work == 128,
                    "expiration admission budget wrong");
            store.expire_due(128, now + 10ms);
            store.set("next", std::string(50000, 'x'), std::nullopt, now + 10ms);
            require(store.used_bytes() == 50200 && store.stats().evictions == 0,
                    "retry after cleanup");
            valid(store, 60000);
        }
        {
            Store store(1000, MemoryPolicy::AllKeysLru);
            for (int i = 0; i < 10000; ++i) {
                store.set("repeat", "value", i % 2 ? std::optional{10ms} : std::nullopt, now);
                valid(store, 1000);
            }
            require(store.size() == 1 && store.recency_size() == 1 && store.expiring_size() == 1,
                    "repeated replacements leaked metadata");
            store.expire_due(128, now + 10ms);
            require(store.used_bytes() == 0 && store.recency_size() == 0,
                    "cleanup retained charge");
        }
        std::cout << "Accounting, write rejection, LRU order, and bounded admission passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
