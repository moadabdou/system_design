#include "store.hpp"
#include <array>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace study {
Store::Store(std::size_t memory_limit, MemoryPolicy policy)
    : memory_limit_(memory_limit), policy_(policy) {
    if (memory_limit == 0 ||
        (policy != MemoryPolicy::NoEviction && policy != MemoryPolicy::AllKeysLru)) {
        throw std::invalid_argument("invalid store memory configuration");
    }
}

std::size_t Store::entry_charge(std::size_t key_bytes, std::size_t value_bytes, bool ttl) {
    const std::size_t overhead = ttl ? 288 : 192;
    const std::size_t copies = ttl ? 3 : 2;
    const auto maximum = std::numeric_limits<std::size_t>::max();
    if (value_bytes > maximum - overhead ||
        key_bytes > (maximum - overhead - value_bytes) / copies) {
        throw MemoryError("entry exceeds memory limit");
    }
    return copies * key_bytes + value_bytes + overhead;
}

[[noreturn]] void Store::reject(const char *message) {
    ++stats_.rejected_writes;
    throw MemoryError(message);
}

bool Store::expired(const Entry &entry, Clock::time_point now) const {
    return entry.deadline && (*entry.deadline)->first <= now;
}

void Store::remove(Entries::iterator entry) {
    if (entry->second.deadline) {
        deadlines_.erase(*entry->second.deadline);
    }
    recency_.erase(entry->second.recency);
    used_bytes_ -= entry->second.charge;
    entries_.erase(entry);
}

const std::string *Store::get(const std::string &key, Clock::time_point now) {
    const auto entry = entries_.find(key);
    if (entry == entries_.end()) {
        ++stats_.misses;
        return nullptr;
    }
    if (expired(entry->second, now)) {
        remove(entry);
        ++stats_.expirations;
        ++stats_.misses;
        return nullptr;
    }
    recency_.splice(recency_.begin(), recency_, entry->second.recency);
    ++stats_.hits;
    return &entry->second.value;
}

void Store::set(const std::string &key, const std::string &value,
                std::optional<std::chrono::milliseconds> ttl, Clock::time_point now) {
    stats_.last_admission_work = 0;
    if (ttl &&
        (ttl->count() <= 0 || *ttl > MAX_TTL ||
         now > Clock::time_point::max() - std::chrono::duration_cast<Clock::duration>(*ttl))) {
        throw std::runtime_error("TTL must be 1..31536000000 milliseconds and fit the clock");
    }
    const auto new_charge = entry_charge(key.size(), value.size(), ttl.has_value());
    if (new_charge > memory_limit_) {
        reject("entry exceeds memory limit");
    }
    auto previous = entries_.find(key);
    if (previous != entries_.end() && expired(previous->second, now)) {
        remove(previous);
        ++stats_.expirations;
        ++stats_.last_admission_work;
        previous = entries_.end();
    }
    const auto old_charge = previous == entries_.end() ? 0 : previous->second.charge;
    const auto fits = [&] { return new_charge <= memory_limit_ - (used_bytes_ - old_charge); };
    while (!fits() && stats_.last_admission_work < ADMISSION_WORK_LIMIT && !deadlines_.empty() &&
           deadlines_.begin()->first <= now) {
        remove(entries_.find(deadlines_.begin()->second));
        ++stats_.expirations;
        ++stats_.last_admission_work;
    }

    // Collect stable list iterators; a later hash-table insertion can rehash.
    std::array<Recency::iterator, ADMISSION_WORK_LIMIT> victims;
    std::size_t victim_count = 0;
    if (!fits()) {
        if (stats_.last_admission_work == ADMISSION_WORK_LIMIT) {
            reject("eviction work limit reached");
        }
        if (policy_ == MemoryPolicy::NoEviction) {
            reject("memory limit reached");
        }
        auto shortage = new_charge - (memory_limit_ - (used_bytes_ - old_charge));
        for (auto candidate = recency_.rbegin(); candidate != recency_.rend() && shortage > 0 &&
                                                 stats_.last_admission_work < ADMISSION_WORK_LIMIT;
             ++candidate) {
            if (*candidate == key) {
                continue;
            }
            const auto charge = entries_.find(*candidate)->second.charge;
            victims[victim_count++] = std::prev(candidate.base());
            ++stats_.last_admission_work;
            shortage = charge >= shortage ? 0 : shortage - charge;
        }
        if (shortage > 0) {
            reject("eviction work limit reached");
        }
    }

    // All throwing work precedes any mutation of live entries. Keep new list
    // metadata in a temporary list, and roll back the timer on insertion failure.
    Recency prepared_recency;
    const auto old_deadline = previous == entries_.end() ? std::nullopt : previous->second.deadline;
    if (previous == entries_.end()) {
        prepared_recency.push_front(key);
    }
    Entry replacement{value, std::nullopt,
                      previous == entries_.end() ? prepared_recency.begin()
                                                 : previous->second.recency,
                      new_charge};
    if (ttl) {
        replacement.deadline = deadlines_.emplace(now + *ttl, key);
    }
    const auto new_deadline = replacement.deadline;
    const auto recency = replacement.recency;
    static_assert(std::is_nothrow_move_assignable_v<Entry>);
    try {
        entries_.insert_or_assign(key, std::move(replacement));
    } catch (...) {
        if (new_deadline) {
            deadlines_.erase(*new_deadline);
        }
        throw;
    }

    // The commit below neither allocates nor throws. The old charge remains in
    // used_bytes_ until victims are removed, avoiding temporary integer overflow.
    if (old_deadline) {
        deadlines_.erase(*old_deadline);
    }
    if (prepared_recency.empty()) {
        recency_.splice(recency_.begin(), recency_, recency);
    } else {
        recency_.splice(recency_.begin(), prepared_recency, recency);
    }
    for (std::size_t i = 0; i < victim_count; ++i) {
        remove(entries_.find(*victims[i]));
        ++stats_.evictions;
    }
    used_bytes_ = used_bytes_ - old_charge + new_charge;
}

bool Store::erase(const std::string &key, Clock::time_point now) {
    const auto entry = entries_.find(key);
    if (entry == entries_.end()) {
        return false;
    }
    const bool was_live = !expired(entry->second, now);
    remove(entry);
    if (!was_live) {
        ++stats_.expirations;
    }
    return was_live;
}

std::size_t Store::expire_due(std::size_t budget, Clock::time_point now) {
    std::size_t removed = 0;
    while (removed < budget && !deadlines_.empty() && deadlines_.begin()->first <= now) {
        remove(entries_.find(deadlines_.begin()->second));
        ++removed;
    }
    stats_.expirations += removed;
    return removed;
}
} // namespace study
