#pragma once

#include <mutex>

namespace swarmmind {

// Global mutex guarding terminal writes. Two Agent instances can now run
// concurrently on separate threads (e.g. a /parallel REPL command), and a
// single std::cout << call is not guaranteed atomic against another
// thread's — without this, concurrent output interleaves mid-line/
// mid-escape-code. Scope each lock_guard to one print statement, not the
// work around it.
inline std::mutex& io_mutex() {
    static std::mutex m;
    return m;
}

} // namespace swarmmind
