#include "blackboard.h"

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    // Empty blackboard
    {
        swarmmind::Blackboard bb;
        assert(bb.size() == 0);
        assert(bb.read_all().empty());
        std::cout << "[PASS] empty blackboard has zero entries\n";
    }

    // post + read_all preserves order and fields
    {
        swarmmind::Blackboard bb;
        bb.post("agent-a", "observation", "the sky is blue", {"weather"});
        bb.post("agent-b", "question", "why?", {"weather", "curiosity"});

        auto entries = bb.read_all();
        assert(entries.size() == 2);
        assert(entries[0].author == "agent-a");
        assert(entries[0].type == "observation");
        assert(entries[0].content == "the sky is blue");
        assert(entries[0].tags.size() == 1 && entries[0].tags[0] == "weather");
        assert(!entries[0].timestamp.empty());
        assert(entries[1].author == "agent-b");
        std::cout << "[PASS] post + read_all: order and fields correct\n";
    }

    // read_since returns only new entries
    {
        swarmmind::Blackboard bb;
        bb.post("agent-a", "observation", "first", {});
        size_t seen = bb.size();

        assert(bb.read_since(seen).empty());

        bb.post("agent-a", "observation", "second", {});
        bb.post("agent-b", "observation", "third", {});

        auto fresh = bb.read_since(seen);
        assert(fresh.size() == 2);
        assert(fresh[0].content == "second");
        assert(fresh[1].content == "third");
        std::cout << "[PASS] read_since: returns only entries posted after the given count\n";
    }

    // read_since with a count beyond the current size returns empty, not garbage
    {
        swarmmind::Blackboard bb;
        bb.post("agent-a", "observation", "only one", {});
        assert(bb.read_since(100).empty());
        std::cout << "[PASS] read_since: out-of-range count returns empty\n";
    }

    // read_by_type filters correctly
    {
        swarmmind::Blackboard bb;
        bb.post("agent-a", "observation", "obs 1", {});
        bb.post("agent-a", "question", "q 1", {});
        bb.post("agent-b", "observation", "obs 2", {});

        auto observations = bb.read_by_type("observation");
        assert(observations.size() == 2);
        assert(observations[0].content == "obs 1");
        assert(observations[1].content == "obs 2");

        auto questions = bb.read_by_type("question");
        assert(questions.size() == 1);

        assert(bb.read_by_type("conclusion").empty());
        std::cout << "[PASS] read_by_type: filters and preserves order\n";
    }

    // read_by_tag filters correctly, including entries with multiple tags
    {
        swarmmind::Blackboard bb;
        bb.post("agent-a", "observation", "obs 1", {"topic-x"});
        bb.post("agent-a", "observation", "obs 2", {"topic-y"});
        bb.post("agent-b", "observation", "obs 3", {"topic-x", "topic-y"});

        auto topic_x = bb.read_by_tag("topic-x");
        assert(topic_x.size() == 2);
        assert(topic_x[0].content == "obs 1");
        assert(topic_x[1].content == "obs 3");

        assert(bb.read_by_tag("nonexistent-tag").empty());
        std::cout << "[PASS] read_by_tag: filters entries with the given tag\n";
    }

    // Concurrent posts from multiple threads: no crashes, no lost entries
    {
        swarmmind::Blackboard bb;
        constexpr int kThreads = 8;
        constexpr int kPostsPerThread = 200;

        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; t++) {
            threads.emplace_back([&bb, t]() {
                for (int i = 0; i < kPostsPerThread; i++) {
                    bb.post("agent-" + std::to_string(t), "observation",
                            "post " + std::to_string(i), {});
                }
            });
        }
        for (auto& th : threads) th.join();

        assert(bb.size() == static_cast<size_t>(kThreads * kPostsPerThread));
        assert(bb.read_all().size() == bb.size());
        std::cout << "[PASS] concurrent post(): " << bb.size()
                  << " entries from " << kThreads << " threads, none lost\n";
    }

    std::cout << "\nAll tests passed!\n";
    return 0;
}
