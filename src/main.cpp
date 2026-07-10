#include "agent.h"
#include "blackboard.h"
#include "config.h"
#include "llm_client.h"
#include "tool.h"
#include "tools/read_file.h"
#include "tools/write_file.h"
#include "tools/edit_file.h"
#include "tools/bash.h"
#include "tools/blackboard_post.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <thread>
#include <sys/utsname.h>
#include <unistd.h>

// ANSI color helpers
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GREEN   "\033[32m"

// Environment context appended to the system prompt so the model knows
// where it is (cwd, platform, date) without having to probe with tools
static std::string build_env_info() {
    std::ostringstream oss;
    oss << "\n## Environment\n";

    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        oss << "- Working directory: " << cwd << "\n";
    }

    struct utsname un;
    if (uname(&un) == 0) {
        oss << "- Platform: " << un.sysname << " " << un.release
            << " (" << un.machine << ")\n";
    }

    std::time_t t = std::time(nullptr);
    char date_buf[32];
    if (std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", std::localtime(&t))) {
        oss << "- Date: " << date_buf << "\n";
    }

    return oss.str();
}

// Each agent gets its own tool registry (each instance is cheap; this
// avoids any question of whether tool state is safe to share across the
// threads two concurrently-running agents use).
static std::shared_ptr<swarmmind::ToolRegistry> make_tools() {
    auto tools = std::make_shared<swarmmind::ToolRegistry>();
    tools->register_tool(std::make_unique<swarmmind::ReadFileTool>());
    tools->register_tool(std::make_unique<swarmmind::WriteFileTool>());
    tools->register_tool(std::make_unique<swarmmind::EditFileTool>());
    tools->register_tool(std::make_unique<swarmmind::BashTool>());
    return tools;
}

// Orchestrator prompt: only has blackboard_post, no filesystem/shell access.
// Decomposes user requests into sub-tasks posted to the blackboard.
static const char* ORCHESTRATOR_PROMPT = R"(
You are the Orchestrator in a multi-agent system. Your tools are
blackboard_post, read_file, and bash. You cannot write or edit files.

## CRITICAL: your role
You are a PLANNER, not a doer. Your ONLY job is to explore just enough
to decompose wisely, then post sub-tasks. The workers will do the rest.

- Use bash/read_file ONLY to get a rough picture of the project
  (e.g. ls the top level, read the README, skim CMakeLists.txt).
  This is reconnaissance, NOT analysis. Keep it fast — 1-3 quick looks.
- Once you understand the project, IMMEDIATELY stop exploring and post
  concise sub-tasks for the workers. Do NOT do the actual analysis
  yourself. Do NOT read every file. Do NOT answer the user's question
  directly — that is the workers' job.
- The user asked a question. You plan how to answer it. Workers execute.
  You do not execute.

## How to decompose
Post 2-5 sub-tasks (blackboard_post, type "task", tags ["pending","<topic>"]).
Each must be specific and completable in one worker turn.

## Example
Bad:  4 tasks all saying "explore the project structure"
Good: "Read src/agent.cpp and trace the SSE parsing flow from chat() through write_callback"
      "Grep for all uses of io_mutex and analyze the locking strategy"
      "Count lines of code per module and summarize the project size"

## Rules
- 2-5 sub-tasks, no more. Each specific and actionable.
- Tag every task with "pending" plus a topic keyword.
- After posting ALL sub-tasks, say one short sentence summarizing the plan.
  Do NOT keep exploring — post tasks and stop.
)";

static std::string trim(const std::string& s) {
    auto first = s.find_first_not_of(" \t\r\n");
    auto last = s.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, last - first + 1);
}

static void print_banner() {
    std::cout << COLOR_CYAN << COLOR_BOLD
              << "╔══════════════════════════════════╗\n"
              << "║         SwarmMind v0.1           ║\n"
              << "║   Multi-Agent Architecture Lab   ║\n"
              << "╚══════════════════════════════════╝\n"
              << COLOR_RESET << "\n"
              << "/exit  /clear  /blackboard\n\n";
}

static void run_repl(swarmmind::Agent& orchestrator,
                     std::shared_ptr<swarmmind::LLMClient> client,
                     std::shared_ptr<swarmmind::Blackboard> blackboard) {
    print_banner();

    std::string line;
    while (true) {
        std::cout << COLOR_GREEN "> " COLOR_RESET << std::flush;
        if (!std::getline(std::cin, line)) {
            break;  // EOF
        }

        line = trim(line);

        if (line == "/exit" || line == "/quit") {
            std::cout << "Goodbye.\n";
            break;
        }

        if (line == "/clear") {
            orchestrator.reset();
            std::cout << "Cleared.\n";
            continue;
        }

        if (line == "/blackboard") {
            auto entries = blackboard->read_all();
            if (entries.empty()) {
                std::cout << "(empty)\n";
            } else {
                std::cout << swarmmind::format_blackboard_context(entries, /*max_entries=*/0);
            }
            continue;
        }

        if (line.empty()) continue;

        // ── Default pipeline: orchestrator → workers → summary ──

        // Snapshot blackboard size before orchestrator runs — only tasks
        // posted THIS turn will be dispatched to workers. Stale entries
        // from previous runs are ignored.
        size_t bb_snapshot = blackboard->size();

        std::cout << "\n\033[1m── Orchestrator: planning ──\033[0m\n";
        orchestrator.run(line);

        auto all_entries = blackboard->read_all();
        std::vector<std::string> task_contents;
        for (size_t i = bb_snapshot; i < all_entries.size(); i++) {
            if (all_entries[i].type == "task")
                task_contents.push_back(all_entries[i].content);
        }

        if (task_contents.empty()) {
            std::cout << "\n\n";
            continue;
        }

        std::cout << "\033[1m── " << task_contents.size()
                  << " new tasks → " << task_contents.size()
                  << " workers ──\033[0m\n";

        auto t0 = std::chrono::steady_clock::now();
        std::vector<std::thread> threads;
        for (size_t i = 0; i < task_contents.size(); i++) {
            std::string name = "w" + std::to_string(i + 1);
            std::string task = task_contents[i];
            threads.emplace_back([client, blackboard, name, task]() {
                auto tools = make_tools();
                tools->register_tool(
                    std::make_unique<swarmmind::BlackboardPostTool>(blackboard, name));
                swarmmind::Agent worker(client, tools);
                worker.set_blackboard(blackboard, name);
                worker.set_max_tool_rounds(10);
                worker.run(
                    "Execute this task and post your result to the blackboard:\n\n" +
                    task + "\n\n"
                    "When done, use blackboard_post to post your findings:\n"
                    "  type: \"result\"\n"
                    "  content: \"<your detailed findings>\"\n"
                    "  tags: [\"done\"]\n\n"
                    "Then say \"done\" — nothing more."
                );
            });
        }
        for (auto& t : threads) t.join();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        std::cout << "\033[2m[all " << task_contents.size()
                  << " workers finished in " << elapsed << "ms]\033[0m\n";

        std::cout << "\n\033[1m── Orchestrator: summary ──\033[0m\n";
        orchestrator.run(
            "Read the blackboard — especially the 'result' entries — and "
            "summarize what was accomplished. Be concise. Do NOT post new tasks."
        );
        std::cout << "\n\n";
    }
}

static std::string read_stdin() {
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    return oss.str();
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "  Interactive REPL: type a request → orchestrator plans →\n"
              << "  workers execute concurrently → orchestrator summarizes.\n"
              << "  Pipe input for a single-turn query.\n"
              << "\nOptions:\n"
              << "  --model <name>   Model to use (default: gpt-4o)\n"
              << "  --config <path>  Path to config.yaml (default: <project root>/config.yaml)\n"
              << "  --help, -h       Show this help\n"
              << "\nConfig (project root's config.yaml, see config.example.yaml):\n"
              << "  openai.api_key    API key (required, or via OPENAI_API_KEY)\n"
              << "  openai.base_url   Base URL (default: https://api.openai.com)\n"
              << "  openai.model      Model name (default: gpt-4o)\n"
              << "\nPrecedence: --model flag > env var > config.yaml > default.\n"
              << "\nExamples:\n"
              << "  swarmmind                           # interactive REPL\n"
              << "  echo \"List files\" | swarmmind       # single-turn via pipe\n"
              << "  swarmmind --model gpt-4o\n";
}

int main(int argc, char* argv[]) {
    // Parse CLI args
    std::string model_override;
    std::string config_path;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_override = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    swarmmind::Config cfg = swarmmind::resolve_config(argv[0], config_path, model_override);

    // Initialize components
    auto client = std::make_shared<swarmmind::LLMClient>();
    client->set_api_key(cfg.api_key);
    if (!cfg.base_url.empty()) {
        client->set_base_url(cfg.base_url);
    }
    client->set_model(cfg.model);

    // Shared blackboard — created first so tools can reference it.
    auto blackboard = std::make_shared<swarmmind::Blackboard>();
    std::string env_info = build_env_info();

    // Orchestrator: blackboard_post + read_file + bash.
    auto orchestrator_tools = std::make_shared<swarmmind::ToolRegistry>();
    orchestrator_tools->register_tool(
        std::make_unique<swarmmind::BlackboardPostTool>(blackboard, "orchestrator"));
    orchestrator_tools->register_tool(std::make_unique<swarmmind::ReadFileTool>());
    orchestrator_tools->register_tool(std::make_unique<swarmmind::BashTool>());
    swarmmind::Agent orchestrator(client, orchestrator_tools);
    orchestrator.set_system_prompt(std::string(ORCHESTRATOR_PROMPT) + env_info);
    orchestrator.set_max_tool_rounds(5);
    orchestrator.set_blackboard(blackboard, "orchestrator");

    // Worker agents are created dynamically — one per task, each with its
    // own ToolRegistry, running concurrently in a single round. No fixed
    // agent pool needed.

    // Auto-detect: pipe input → single-shot; tty → REPL
    bool is_pipe = !isatty(STDIN_FILENO);

    if (is_pipe) {
        std::string input = read_stdin();

        if (input.empty()) {
            return 0;
        }

        // Trim trailing newlines
        while (!input.empty() && (input.back() == '\n' || input.back() == '\r')) {
            input.pop_back();
        }

        if (input.empty()) {
            return 0;
        }

        orchestrator.run(input);
        std::cout << "\n";
    } else {
        run_repl(orchestrator, client, blackboard);
    }

    return 0;
}
