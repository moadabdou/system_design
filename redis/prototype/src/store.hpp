#pragma once
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace study {
// Values and deadline metadata are owned by the event-loop thread.
class Store {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto MAX_TTL = std::chrono::milliseconds{31536000000LL};
    Store() = default;
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

  private:
    using Deadlines = std::multimap<Clock::time_point, std::string>;
    struct Entry {
        std::string value;
        std::optional<Deadlines::iterator> deadline;
    };
    using Entries = std::unordered_map<std::string, Entry>;
    void remove(Entries::iterator entry);
    bool expired(const Entry &entry, Clock::time_point now) const;
    Deadlines deadlines_;
    Entries entries_;
};
} // namespace study
