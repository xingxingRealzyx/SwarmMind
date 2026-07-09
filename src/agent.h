#pragma once

#include "types.h"
#include "llm_client.h"
#include "tool.h"
#include "blackboard.h"

#include <string>
#include <vector>
#include <memory>

namespace swarmmind {

class Agent {
public:
    // tools: native function calling — request/response, ends the round.
    Agent(std::shared_ptr<LLMClient> client,
          std::shared_ptr<ToolRegistry> tools);

    // Set the system prompt
    void set_system_prompt(const std::string& prompt) { system_prompt_ = prompt; }

    // Run a single conversation turn with the given user input.
    // Manages history across calls (append-only).
    // Returns the final assistant text response.
    std::string run(const std::string& user_input);

    // Clear conversation history (keeps system prompt)
    void reset();

    // Max tool-call round trips per turn
    void set_max_tool_rounds(int n) { max_tool_rounds_ = n; }

    // Attaches a shared blackboard. Each turn, the latest entries are
    // auto-injected into the system prompt (no tool call needed to read),
    // and this agent may post to it via an inline <cmd>blackboard_post</cmd>
    // embedded in its reply text (no tool-call round trip either — posting
    // is a fast, synchronous, fire-and-forget in-memory append). `name`
    // identifies this agent as the author of its postings.
    void set_blackboard(std::shared_ptr<Blackboard> blackboard, std::string name);

private:
    std::shared_ptr<LLMClient> client_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<Blackboard> blackboard_;
    std::string agent_name_ = "agent";

    std::string system_prompt_;
    std::vector<Message> history_;  // conversation history (excl. system prompt)
    int max_tool_rounds_ = 10;

    // Build the full messages array for API call (system prompt + history,
    // plus blackboard context appended to the system prompt if attached)
    std::vector<Message> build_messages() const;

    // Execute tool calls and return the tool result messages
    std::vector<Message> execute_tools(const std::vector<ToolCall>& tool_calls);

    // Parses and dispatches one complete inline command payload. Currently
    // only "blackboard_post" is recognized; unknown commands are logged
    // and ignored. Returns true if the payload was recognized and handled.
    bool handle_inline_command(const std::string& payload);
};

// Default system prompt for the agent
inline const char* DEFAULT_SYSTEM_PROMPT = R"(
You are an interactive AI agent that helps users with software engineering tasks.

You have access to the following capabilities:
- Read files from the filesystem
- Write content to files
- Edit files with exact string replacement
- Execute bash shell commands

## Guidelines
- Be concise and direct in your responses.
- When asked to make code changes, use the tools to read files first, then edit or write.
- When editing files, ensure you preserve exact indentation and formatting.
- Use absolute paths when referring to files.
- If a bash command might be destructive, ask for confirmation first.
- Report outcomes faithfully: if something fails, say so.
)";

} // namespace swarmmind
