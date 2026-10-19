#include "store.hpp"
#include <stdexcept>
#include <utility>

namespace study {
bool Store::expired(const Entry &entry, Clock::time_point now) const {
    return entry.deadline && (*entry.deadline)->first <= now;
}

void Store::remove(Entries::iterator entry) {
    if (entry->second.deadline) {
        deadlines_.erase(*entry->second.deadline);
    }
    entries_.erase(entry);
}

const std::string *Store::get(const std::string &key, Clock::time_point now) {
    const auto entry = entries_.find(key);
    if (entry == entries_.end()) {
        return nullptr;
    }
    if (expired(entry->second, now)) {
        remove(entry);
        return nullptr;
    }
    return &entry->second.value;
}

void Store::set(const std::string &key, const std::string &value,
                std::optional<std::chrono::milliseconds> ttl, Clock::time_point now) {
    if (ttl &&
        (ttl->count() <= 0 || *ttl > MAX_TTL ||
         now > Clock::time_point::max() - std::chrono::duration_cast<Clock::duration>(*ttl))) {
        throw std::runtime_error("TTL must be 1..31536000000 milliseconds and fit the clock");
    }
    Entry replacement{value, std::nullopt};
    if (ttl) {
        replacement.deadline = deadlines_.emplace(now + *ttl, key);
    }
    const auto previous = entries_.find(key);
    const auto old_deadline = previous == entries_.end() ? std::nullopt : previous->second.deadline;
    const auto new_deadline = replacement.deadline;
    try {
        entries_.insert_or_assign(key, std::move(replacement));
    } catch (...) {
        if (new_deadline) {
            deadlines_.erase(*new_deadline);
        }
        throw;
    }
    if (old_deadline) {
        deadlines_.erase(*old_deadline);
    }
}

bool Store::erase(const std::string &key, Clock::time_point now) {
    const auto entry = entries_.find(key);
    if (entry == entries_.end()) {
        return false;
    }
    const bool was_live = !expired(entry->second, now);
    remove(entry);
    return was_live;
}

std::size_t Store::expire_due(std::size_t budget, Clock::time_point now) {
    std::size_t removed = 0;
    while (removed < budget && !deadlines_.empty() && deadlines_.begin()->first <= now) {
        remove(entries_.find(deadlines_.begin()->second));
        ++removed;
    }
    return removed;
}
} // namespace study
