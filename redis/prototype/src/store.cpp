#include "store.hpp"

namespace study {
const std::string *Store::get(const std::string &key) const {
    const auto entry = entries_.find(key);
    if (entry == entries_.end()) {
        return nullptr;
    }
    return &entry->second;
}

void Store::set(const std::string &key, const std::string &value) {
    entries_.insert_or_assign(key, value);
}

bool Store::erase(const std::string &key) {
    return entries_.erase(key) != 0;
}
} // namespace study
