#include "store.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using study::Store;
using namespace std::chrono_literals;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main() {
    try {
        const auto start = Store::Clock::time_point{};
        Store store;
        store.set("boundary", "value", 10ms, start);
        require(store.get("boundary", start + 10ms - 1ns) != nullptr, "expired too early");
        require(store.get("boundary", start + 10ms) == nullptr, "live at deadline");
        require(store.size() == 0 && store.expiring_size() == 0, "GET leaked entry or timer");
        store.set("deleted", "value", 10ms, start);
        require(!store.erase("deleted", start + 10ms), "expired DEL returned one");
        require(store.size() == 0 && store.expiring_size() == 0, "DEL leaked entry or timer");
        store.set("live", "value", 10ms, start);
        require(store.erase("live", start + 9ms), "live DEL returned zero");
        require(!store.erase("missing", start), "missing DEL returned one");

        store.set("replace", "old", 10ms, start);
        store.set("replace", "new", 20ms, start + 5ms);
        require(store.expiring_size() == 1, "replacement leaked timer");
        require(store.expire_due(128, start + 10ms) == 0, "old timer deleted replacement");
        require(*store.get("replace", start + 24ms) == "new", "wrong replacement");
        require(store.get("replace", start + 25ms) == nullptr, "replacement deadline wrong");
        store.set("persistent", "old", 10ms, start);
        store.set("persistent", "new", std::nullopt, start + 5ms);
        require(store.expiring_size() == 0, "plain SET did not clear TTL");
        require(*store.get("persistent", start + 1h) == "new", "plain SET expired");
        store.set("persistent", "ttl", 1ms, start + 1h);
        require(store.get("persistent", start + 1h + 1ms) == nullptr, "TTL not added");

        store.set("keep", "original", 100ms, start);
        for (const auto ttl : {0ms, -1ms, Store::MAX_TTL + 1ms}) {
            bool rejected = false;
            try {
                store.set("keep", "bad", ttl, start);
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            require(rejected, "invalid TTL accepted");
            require(*store.get("keep", start) == "original", "invalid TTL mutated value");
            require(store.expiring_size() == 1, "invalid TTL mutated timer");
        }
        require(store.get("keep", start + 100ms) == nullptr, "invalid TTL changed deadline");
        store.set("maximum", "ok", Store::MAX_TTL, start);
        require(store.get("maximum", start + Store::MAX_TTL) == nullptr, "maximum TTL wrong");
        bool rejected_overflow = false;
        try {
            store.set("overflow", "bad", 1ms, Store::Clock::time_point::max());
        } catch (const std::runtime_error &) {
            rejected_overflow = true;
        }
        require(rejected_overflow && store.size() == 0, "clock overflow not rejected");

        // Repeated updates must retain exactly one timer, including shorter TTLs.
        for (int i = 0; i < 10000; ++i) {
            store.set("updated", "v", std::chrono::milliseconds{10001 - i}, start);
        }
        require(store.size() == 1 && store.expiring_size() == 1, "update timer growth");
        require(store.erase("updated", start), "updated key missing");
        require(store.expiring_size() == 0, "delete retained timer");

        for (int i = 0; i < 1000; ++i) {
            store.set(std::to_string(i), "untouched", 10ms, start);
        }
        store.set("future", "v", 20ms, start);
        store.set("permanent", "v", std::nullopt, start);
        require(store.expire_due(128, start + 9ms) == 0, "cleanup ran early");
        require(store.expire_due(0, start + 10ms) == 0, "zero budget ignored");
        require(store.expire_due(128, start + 10ms) == 128, "cleanup budget wrong");
        require(store.size() == 874 && store.expiring_size() == 873, "reclamation count wrong");
        std::size_t total = 128;
        for (int pass = 0; pass < 7; ++pass) {
            const auto removed = store.expire_due(128, start + 10ms);
            require(removed <= 128, "cleanup exceeded budget");
            total += removed;
        }
        require(total == 1000 && store.size() == 2 && store.expiring_size() == 1,
                "untouched entries or timers remain");
        require(store.expire_due(128, start + 20ms) == 1, "future key not reclaimed");
        require(store.size() == 1 && store.expiring_size() == 0, "permanent entry removed");
        std::cout
            << "Expiration boundaries, replacements, validation, and bounded reclamation passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
