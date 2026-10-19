#include "store.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

// Fail one allocation at each preparation step, then restore allocation so
// cleanup and diagnostics can run normally. No production allocator is changed.
long allocations_before_failure = -1;
void *operator new(std::size_t bytes) {
    if (allocations_before_failure == 0) {
        allocations_before_failure = -1;
        throw std::bad_alloc();
    }
    if (allocations_before_failure > 0) {
        --allocations_before_failure;
    }
    if (void *memory = std::malloc(bytes == 0 ? 1 : bytes)) {
        return memory;
    }
    throw std::bad_alloc();
}
void operator delete(void *memory) noexcept {
    std::free(memory);
}
void operator delete(void *memory, std::size_t) noexcept {
    std::free(memory);
}

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("allocation failure changed live data or metadata");
    }
}

int main() {
    try {
        using namespace std::chrono_literals;
        using study::Store;
        const auto now = Store::Clock::time_point{};
        const std::string a(32, 'a'), b(32, 'b'), c(32, 'c');
        const std::string original(100, 'v'), replacement(500, 'r');
        for (const bool replacing : {false, true}) {
            int failures = 0;
            bool succeeded = false;
            for (long fail_at = 0; fail_at < 20; ++fail_at) {
                Store store(1000, study::MemoryPolicy::AllKeysLru);
                store.set(a, original, 100ms, now);
                store.set(b, original, std::nullopt, now);
                bool failed = false;
                allocations_before_failure = fail_at;
                try {
                    store.set(replacing ? a : c, replacement, 200ms, now);
                } catch (const std::bad_alloc &) {
                    failed = true;
                }
                allocations_before_failure = -1;
                if (!failed) {
                    require(store.used_bytes() == 884 && store.size() == 1 &&
                            store.recency_size() == 1 && store.expiring_size() == 1);
                    succeeded = true;
                    break;
                }
                ++failures;
                require(store.used_bytes() == 840 && store.size() == 2 &&
                        store.expiring_size() == 1 && store.recency_size() == 2 &&
                        store.stats().evictions == 0);
                require(*store.get(a, now + 99ms) == original);
                require(*store.get(b, now + 99ms) == original); // Restore b,a order.
                // A later successful insertion must evict the original LRU key,
                // proving failed staging did not move it in the recency list.
                store.set(c, original, std::nullopt, now);
                require(store.get(a, now) == nullptr && *store.get(b, now) == original);
            }
            require(failures >= 2 && succeeded);
        }
        std::cout << "Allocation failures preserve entries, timers, accounting, and LRU order\n";
    } catch (const std::exception &error) {
        allocations_before_failure = -1;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
