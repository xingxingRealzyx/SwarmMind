#pragma once

#include <functional>
#include <string>
#include <nlohmann/json.hpp>

namespace swarmmind {

// Inline commands are fire-and-forget actions an agent embeds directly in
// its reply text, executed synchronously the instant the closing tag
// streams in — no tool-call round trip, so they never add an extra LLM
// turn or block the agent waiting for a result:
//
//   <cmd>{"name":"blackboard_post","args":{"type":"observation","content":"..."}}</cmd>
//
// XML-style tags (not a bare JSON blob) give the parser an unambiguous
// boundary to look for while streaming.
inline constexpr const char* kCmdOpen = "<cmd>";
inline constexpr const char* kCmdClose = "</cmd>";

// Incremental parser that splits a streamed text flow into pass-through
// display text and complete <cmd>...</cmd> command payloads. Handles
// markers split across arbitrary chunk boundaries. An unterminated command
// degrades back to plain text on flush() — nothing is silently lost.
class InlineCommandParser {
public:
    using CommandHandler = std::function<void(const std::string& payload)>;

    InlineCommandParser(std::string open_marker, std::string close_marker,
                        CommandHandler handler);

    // Feed a text delta; returns the text to display. Invokes the handler
    // for each command block completed within this delta.
    std::string feed(const std::string& delta);

    // End of stream: returns any held-back text (partial marker or
    // unterminated command, restored verbatim) and resets the parser.
    std::string flush();

private:
    enum class State { TEXT, OPEN_MATCH, PAYLOAD, CLOSE_MATCH };

    void step(char c, std::string& out);

    std::string open_;
    std::string close_;
    CommandHandler handler_;

    State state_ = State::TEXT;
    std::string match_;    // partially matched marker
    std::string payload_;  // command body being accumulated
};

// Structural recovery for a command payload whose JSON failed strict
// parsing. Targets the shape {"name":"X","args":{"key":"..."}} (single
// string arg): locates "name" and the first args key structurally, then
// takes the value from its opening quote through the payload's LAST quote
// — so unescaped quotes inside content (e.g. dialogue in a story) and a
// missing trailing brace can't defeat it. Recovers the first arg only.
// Returns false if the structure can't be located at all.
bool extract_loose_command(const std::string& payload,
                           std::string& name, nlohmann::json& args);

} // namespace swarmmind
