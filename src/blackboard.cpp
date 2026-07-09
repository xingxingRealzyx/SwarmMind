#include "blackboard.h"

#include <algorithm>
#include <ctime>

namespace swarmmind {

namespace {
std::string now_timestamp() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t))) {
        return buf;
    }
    return "";
}
}  // namespace

void Blackboard::post(const std::string& author, const std::string& type,
                       const std::string& content,
                       const std::vector<std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.push_back(BlackboardEntry{author, type, content, tags, now_timestamp()});
}

std::vector<BlackboardEntry> Blackboard::read_all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

std::vector<BlackboardEntry> Blackboard::read_since(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count >= entries_.size()) return {};
    return std::vector<BlackboardEntry>(entries_.begin() + static_cast<long>(count), entries_.end());
}

std::vector<BlackboardEntry> Blackboard::read_by_type(const std::string& type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BlackboardEntry> result;
    std::copy_if(entries_.begin(), entries_.end(), std::back_inserter(result),
                 [&type](const BlackboardEntry& e) { return e.type == type; });
    return result;
}

std::vector<BlackboardEntry> Blackboard::read_by_tag(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BlackboardEntry> result;
    std::copy_if(entries_.begin(), entries_.end(), std::back_inserter(result),
                 [&tag](const BlackboardEntry& e) {
                     return std::find(e.tags.begin(), e.tags.end(), tag) != e.tags.end();
                 });
    return result;
}

size_t Blackboard::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::string format_blackboard_context(const std::vector<BlackboardEntry>& entries,
                                      size_t max_entries) {
    if (entries.empty()) return "";

    size_t start = (max_entries > 0 && entries.size() > max_entries)
        ? entries.size() - max_entries
        : 0;

    std::string out;
    for (size_t i = start; i < entries.size(); i++) {
        const auto& e = entries[i];
        out += "- [" + e.timestamp + "] " + e.author + " (" + e.type + "): " + e.content;
        if (!e.tags.empty()) {
            out += " [tags: ";
            for (size_t t = 0; t < e.tags.size(); t++) {
                if (t > 0) out += ", ";
                out += e.tags[t];
            }
            out += "]";
        }
        out += "\n";
    }
    return out;
}

} // namespace swarmmind
