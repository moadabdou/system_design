#pragma once
#include <string>
#include <unordered_map>

namespace study {
// The step-4 store is simply a hash table of byte strings.
class Store {
  public:
    // The returned value is borrowed until this key is changed or removed.
    const std::string *get(const std::string &key) const;
    void set(const std::string &key, const std::string &value);
    bool erase(const std::string &key);

  private:
    std::unordered_map<std::string, std::string> entries_;
};
} // namespace study
