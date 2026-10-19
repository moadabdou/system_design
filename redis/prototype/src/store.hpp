#pragma once
#include "config.hpp"
#include <chrono>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace study {
class MemoryError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Values and deadline metadata are owned by the event-loop thread.
class Store {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto MAX_TTL = std::chrono::milliseconds{31536000000LL};
    static constexpr std::size_t ADMISSION_WORK_LIMIT = 128;
    struct Stats {
        std::size_t hits = 0;
        std::size_t misses = 0;
        std::size_t rejected_writes = 0;
        std::size_t evictions = 0;
        std::size_t expirations = 0;
        std::size_t last_admission_work = 0;
    };
    explicit Store(std::size_t memory_limit = DEFAULT_MEMORY_LIMIT,
                   MemoryPolicy policy = MemoryPolicy::NoEviction);
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    // The returned value is borrowed until this key is changed or removed.
    const std::string *get(const std::string &key, Clock::time_point now = Clock::now());
    void set(const std::string &key, const std::string &value,
             std::optional<std::chrono::milliseconds> ttl = std::nullopt,
             Clock::time_point now = Clock::now());
    bool erase(const std::string &key, Clock::time_point now = Clock::now());
    std::size_t expire_due(std::size_t budget, Clock::time_point now = Clock::now());
    // Physical entries, including expired keys awaiting cleanup.
    std::size_t size() const {
        return entries_.size();
    }
    std::size_t expiring_size() const {
        return deadlines_.size();
    }
    std::size_t used_bytes() const {
        return used_bytes_;
    }
    std::size_t recency_size() const {
        return recency_.size();
    }
    const Stats &stats() const {
        return stats_;
    }
    static std::size_t entry_charge(std::size_t key_bytes, std::size_t value_bytes, bool ttl);

  private:
    using Deadlines = std::multimap<Clock::time_point, std::string>;
    using Recency = std::list<std::string>;
    struct Entry {
        std::string value;
        std::optional<Deadlines::iterator> deadline;
        Recency::iterator recency;
        std::size_t charge;
    };
    using Entries = std::unordered_map<std::string, Entry>;
    void remove(Entries::iterator entry);
    bool expired(const Entry &entry, Clock::time_point now) const;
    [[noreturn]] void reject(const char *message);
    Deadlines deadlines_;
    Recency recency_;
    Entries entries_;
    std::size_t memory_limit_;
    MemoryPolicy policy_;
    std::size_t used_bytes_ = 0;
    Stats stats_;
};
} // namespace study
