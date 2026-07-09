#include "agent.h"
#include "console.h"
#include "inline_command.h"

#include <chrono>
#include <iostream>
#include <sstream>

namespace swarmmind {

Agent::Agent(std::shared_ptr<LLMClient> client,
             std::shared_ptr<ToolRegistry> tools)
    : client_(std::move(client))
    , tools_(std::move(tools))
    , system_prompt_(DEFAULT_SYSTEM_PROMPT)
{
}

void Agent::reset() {
    history_.clear();
}

void Agent::set_blackboard(std::shared_ptr<Blackboard> blackboard, std::string name) {
    blackboard_ = std::move(blackboard);
    agent_name_ = std::move(name);
}

std::vector<Message> Agent::build_messages() const {
    std::vector<Message> messages;

    std::string sys = system_prompt_;
    if (blackboard_) {
        sys += "\n\n## Shared Blackboard\n"
               "You are \"" + agent_name_ + "\" in a multi-agent workspace. Other agents "
               "post to a shared blackboard; the latest entries are listed below. "
               "Use the blackboard_post tool to post your own entries. Choose \"type\" "
               "freely (e.g. task, claim, result, observation, question, conclusion).\n\n";
        std::string ctx = format_blackboard_context(blackboard_->read_all());
        sys += ctx.empty() ? "(blackboard is empty)" : ctx;
    }

    if (!sys.empty()) {
        messages.push_back(Message::system(sys));
    }

    // History (already includes the current user input)
    for (const auto& msg : history_) {
        messages.push_back(msg);
    }

    return messages;
}

std::vector<Message> Agent::execute_tools(const std::vector<ToolCall>& tool_calls) {
    std::vector<Message> results;
    for (const auto& tc : tool_calls) {
        // Show the tool name and its arguments (dimmed, truncated if long)
        std::string args_preview = tc.arguments.empty() ? "{}" : tc.arguments;
        if (args_preview.size() > 300) {
            args_preview.resize(300);
            args_preview += "...";
        }
        {
            std::lock_guard<std::mutex> io_lock(io_mutex());
            std::cout << "\n\033[36m[" << agent_name_ << "] [Tool: " << tc.name << "]\033[0m \033[2m"
                      << args_preview << "\033[0m\n";
        }

        // Parse arguments from JSON string
        nlohmann::json args;
        try {
            if (!tc.arguments.empty()) {
                args = nlohmann::json::parse(tc.arguments);
            }
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "\033[31m[Error parsing tool arguments: " << e.what() << "]\033[0m\n";
            results.push_back(Message::tool_result(tc.id,
                "Error: failed to parse arguments: " + std::string(e.what())));
            continue;
        }

        std::string output = tools_->execute(tc.name, args);

        // Print first few lines of output for visibility
        {
            std::lock_guard<std::mutex> io_lock(io_mutex());
            std::istringstream stream(output);
            std::string line;
            int preview_lines = 0;
            while (std::getline(stream, line)) {
                if (preview_lines >= 5) {
                    std::cout << "  ... (truncated)\n";
                    break;
                }
                std::cout << "  " << line << "\n";
                preview_lines++;
            }
        }

        results.push_back(Message::tool_result(tc.id, output));
    }
    return results;
}

bool Agent::handle_inline_command(const std::string& payload) {
    // No inline commands are currently registered — the framework is kept
    // as a hook for future fire-and-forget features.  Log unknown payloads
    // so a stray <cmd> in the model's output doesn't silently vanish.
    std::cerr << "\033[2m[inline command (no handler): " << payload << "]\033[0m\n";
    return false;
}

std::string Agent::run(const std::string& user_input) {
    if (user_input.empty()) {
        return "";
    }

    auto start_time = std::chrono::steady_clock::now();
    std::string final_response;
    int round = 0;

    // Record the user message in history, then build the request from it
    history_.push_back(Message::user(user_input));
    std::vector<Message> llm_messages = build_messages();

    // Multiple agents may now stream to the terminal concurrently (e.g. a
    // /parallel REPL command) — tag each response with its agent name so
    // interleaved output is still attributable.
    if (blackboard_) {
        std::lock_guard<std::mutex> io_lock(io_mutex());
        std::cout << "\n\033[1m[" << agent_name_ << "] »\033[0m ";
    }

    while (round < max_tool_rounds_) {
        round++;

        // Get tools schema
        nlohmann::json tools_schema = tools_->tools_schema();

        // Inline commands cannot span rounds — fresh parser per round. It
        // splits the streamed text into display text (printed as before)
        // and <cmd>...</cmd> payloads (dispatched to handle_inline_command,
        // e.g. blackboard_post) without altering the visible reply.
        InlineCommandParser cmd_parser(kCmdOpen, kCmdClose,
            [this](const std::string& payload) { handle_inline_command(payload); });

        // Call LLM with streaming
        ChatResult result = client_->chat(llm_messages, tools_schema,
            [&cmd_parser](const StreamEvent& event) {
                switch (event.type) {
                case StreamEventType::TEXT_DELTA: {
                    std::string display = cmd_parser.feed(event.text);
                    std::lock_guard<std::mutex> io_lock(io_mutex());
                    std::cout << display << std::flush;
                    break;
                }
                case StreamEventType::TOOL_CALL_DELTA:
                    // Not printed — tool execution is displayed separately
                    break;
                case StreamEventType::FINISH: {
                    std::string display = cmd_parser.flush();
                    std::lock_guard<std::mutex> io_lock(io_mutex());
                    std::cout << display << std::flush;
                    if (event.finish_reason == "tool_calls") {
                        std::cout << "\n\033[33m[Calling tools...]\033[0m\n";
                    }
                    break;
                }
                case StreamEventType::ERROR:
                    std::cerr << "\033[31m[Stream error]\033[0m\n";
                    break;
                }
                return true;
            });

        // Safety net if the stream ended without a FINISH event
        {
            std::string display = cmd_parser.flush();
            std::lock_guard<std::mutex> io_lock(io_mutex());
            std::cout << display << std::flush;
        }

        if (!result.success) {
            std::cerr << "\033[31m[LLM error: " << result.error_message << "]\033[0m\n";
            final_response = "Error: " + result.error_message;
            break;
        }

        // No tool calls — this is the final answer
        if (result.tool_calls.empty()) {
            if (!result.content.empty()) {
                history_.push_back(Message::assistant(result.content));
                final_response = result.content;
            }
            break;
        }

        // Assistant message with tool calls (may also carry text content)
        Message assistant_msg = Message::assistant_tool_calls(result.tool_calls, result.content);
        history_.push_back(assistant_msg);
        llm_messages.push_back(assistant_msg);

        // Execute tools and feed results back
        auto tool_results = execute_tools(result.tool_calls);
        for (const auto& tr : tool_results) {
            history_.push_back(tr);
            llm_messages.push_back(tr);
        }

        {
            std::lock_guard<std::mutex> io_lock(io_mutex());
            std::cout << "\n";
        }
    }

    if (round >= max_tool_rounds_ && final_response.empty()) {
        final_response = "[Max tool call rounds reached]";
        std::lock_guard<std::mutex> io_lock(io_mutex());
        std::cout << "\033[33m" << final_response << "\033[0m\n";
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    {
        std::lock_guard<std::mutex> io_lock(io_mutex());
        std::cout << "\n\033[2m[" << agent_name_ << " | " << elapsed_ms << "ms]\033[0m\n";
    }

    return final_response;
}

} // namespace swarmmind
