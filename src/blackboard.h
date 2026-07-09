#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace swarmmind {

// One posting to the shared blackboard.
//
// `type` is a free-form category the posting agent chooses (e.g.
// "observation", "question", "conclusion", "task") — not an enum, so new
// kinds of postings don't require touching this header. `tags` are
// free-form labels for filtering (e.g. a topic or a target agent name).
struct BlackboardEntry {
    std::string author;
    std::string type;
    std::string content;
    std::vector<std::string> tags;
    std::string timestamp;  // "%Y-%m-%dT%H:%M:%S", local time
};

// Shared workspace multiple agents read from and write to, instead of
// calling each other directly. Append-only and thread-safe: any number of
// agents may post() and read() concurrently.
class Blackboard {
public:
    // Appends a new entry, stamped with the current local time.
    void post(const std::string& author, const std::string& type,
              const std::string& content,
              const std::vector<std::string>& tags = {});

    // All entries, oldest first.
    std::vector<BlackboardEntry> read_all() const;

    // Entries appended since read_all()/read_since() would have returned
    // `count` entries — i.e. entries at index >= count. Pairs with
    // size(): an agent tracks how many entries it has already seen and
    // passes that count back in to fetch only what's new.
    std::vector<BlackboardEntry> read_since(size_t count) const;

    // Entries whose `type` matches exactly, oldest first.
    std::vector<BlackboardEntry> read_by_type(const std::string& type) const;

    // Entries that carry the given tag (exact match), oldest first.
    std::vector<BlackboardEntry> read_by_tag(const std::string& tag) const;

    // Total number of entries currently posted.
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::vector<BlackboardEntry> entries_;
};

// Renders entries as human/LLM-readable text for injection into a system
// prompt, most recent first, capped to max_entries (0 = no cap) so an
// agent's context doesn't grow unbounded as the blackboard fills up. Pure
// function of its input — no Blackboard instance needed — so it's testable
// without a live agent.
std::string format_blackboard_context(const std::vector<BlackboardEntry>& entries,
                                      size_t max_entries = 20);

} // namespace swarmmind
