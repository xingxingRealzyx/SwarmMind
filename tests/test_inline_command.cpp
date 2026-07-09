#include "inline_command.h"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    // Plain text with no command passes through untouched
    {
        std::vector<std::string> payloads;
        swarmmind::InlineCommandParser parser("<cmd>", "</cmd>",
            [&](const std::string& p) { payloads.push_back(p); });

        std::string out = parser.feed("just a normal reply, nothing special");
        out += parser.flush();
        assert(out == "just a normal reply, nothing special");
        assert(payloads.empty());
        std::cout << "[PASS] plain text passes through, no commands fired\n";
    }

    // A single well-formed command is extracted and stripped from display text
    {
        std::vector<std::string> payloads;
        swarmmind::InlineCommandParser parser("<cmd>", "</cmd>",
            [&](const std::string& p) { payloads.push_back(p); });

        std::string out = parser.feed(
            "before <cmd>{\"name\":\"blackboard_post\",\"args\":{\"content\":\"hi\"}}</cmd> after");
        out += parser.flush();
        assert(out == "before  after");
        assert(payloads.size() == 1);
        assert(payloads[0] == "{\"name\":\"blackboard_post\",\"args\":{\"content\":\"hi\"}}");
        std::cout << "[PASS] single command: extracted and stripped from display text\n";
    }

    // Multiple commands in one reply are all extracted, in order
    {
        std::vector<std::string> payloads;
        swarmmind::InlineCommandParser parser("<cmd>", "</cmd>",
            [&](const std::string& p) { payloads.push_back(p); });

        std::string out = parser.feed(
            "a<cmd>{\"name\":\"one\"}</cmd>b<cmd>{\"name\":\"two\"}</cmd>c");
        out += parser.flush();
        assert(out == "abc");
        assert(payloads.size() == 2);
        assert(payloads[0] == "{\"name\":\"one\"}");
        assert(payloads[1] == "{\"name\":\"two\"}");
        std::cout << "[PASS] multiple commands: all extracted in order\n";
    }

    // A command split across several feed() calls (simulating token-by-token
    // streaming) is still recognized correctly
    {
        std::vector<std::string> payloads;
        std::string out;
        swarmmind::InlineCommandParser parser("<cmd>", "</cmd>",
            [&](const std::string& p) { payloads.push_back(p); });

        out += parser.feed("hel");
        out += parser.feed("lo <c");
        out += parser.feed("md>{\"name\":\"x\",");
        out += parser.feed("\"args\":{}}</c");
        out += parser.feed("md> world");
        out += parser.flush();

        assert(out == "hello  world");
        assert(payloads.size() == 1);
        assert(payloads[0] == "{\"name\":\"x\",\"args\":{}}");
        std::cout << "[PASS] command split across many feed() calls: still recognized\n";
    }

    // A close marker that looks like it's starting but isn't (mismatch mid-tag)
    // degrades back to plain text without losing or corrupting content
    {
        std::vector<std::string> payloads;
        swarmmind::InlineCommandParser parser("<cmd>", "</cmd>",
            [&](const std::string& p) { payloads.push_back(p); });

        // "</c" then "!" breaks the close-tag match — must fall back to
        // treating "</c!" as literal payload content, not silently drop it
        std::string out = parser.feed("<cmd>payload</c!ontent</cmd>tail");
        out += parser.flush();
        assert(payloads.size() == 1);
        assert(payloads[0] == "payload</c!ontent");
        assert(out == "tail");
        std::cout << "[PASS] mismatched close-tag prefix: recovers without corrupting payload\n";
    }

    // An unterminated command (stream cut off, or model forgot the closing
    // tag) degrades back to plain text on flush() instead of being dropped
    {
        std::vector<std::string> payloads;
        swarmmind::InlineCommandParser parser("<cmd>", "</cmd>",
            [&](const std::string& p) { payloads.push_back(p); });

        std::string out = parser.feed("said something <cmd>{\"name\":\"x\"");
        out += parser.flush();
        assert(payloads.empty());
        assert(out == "said something <cmd>{\"name\":\"x\"");
        std::cout << "[PASS] unterminated command: restored verbatim on flush, nothing lost\n";
    }

    // A lone partial open-marker (e.g. reply just ends with "<cm") is also
    // restored verbatim, not swallowed
    {
        std::vector<std::string> payloads;
        swarmmind::InlineCommandParser parser("<cmd>", "</cmd>",
            [&](const std::string& p) { payloads.push_back(p); });

        std::string out = parser.feed("almost there <cm");
        out += parser.flush();
        assert(payloads.empty());
        assert(out == "almost there <cm");
        std::cout << "[PASS] partial open-marker at end of stream: restored verbatim\n";
    }

    // extract_loose_command recovers name + first arg from malformed JSON
    // (e.g. an unescaped quote inside the content breaking strict parsing)
    {
        std::string name;
        nlohmann::json args;
        bool ok = swarmmind::extract_loose_command(
            "{\"name\":\"blackboard_post\",\"args\":{\"content\":\"she said \"hi\" to me\"}}",
            name, args);
        assert(ok);
        assert(name == "blackboard_post");
        assert(args.contains("content"));
        assert(args["content"] == "she said \"hi\" to me");
        std::cout << "[PASS] extract_loose_command: recovers name + arg despite unescaped quotes\n";
    }

    // extract_loose_command fails cleanly (no crash, no garbage) when there's
    // no recognizable command structure at all
    {
        std::string name;
        nlohmann::json args;
        bool ok = swarmmind::extract_loose_command("not a command at all", name, args);
        assert(!ok);
        std::cout << "[PASS] extract_loose_command: returns false on unrecoverable input\n";
    }

    std::cout << "\nAll tests passed!\n";
    return 0;
}
