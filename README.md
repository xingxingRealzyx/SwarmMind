# SwarmMind

> A multi-agent architecture lab in C++17 — shared blackboard, dynamic workers, orchestrator-driven.

## Architecture

```
User Input
    │
    ▼
┌──────────────────────────────────────────────────┐
│  Orchestrator (plans, decomposes into sub-tasks) │
│  Tools: blackboard_post, read_file, bash         │
└────────────────────┬─────────────────────────────┘
                     │  posts N task entries
                     ▼
┌──────────────────────────────────────────────────┐
│  Shared Blackboard (thread-safe, append-only)    │
│  Agents communicate only through the blackboard  │
└────┬──────────────────────────────┬──────────────┘
     │                              │
     ▼                              ▼
┌──────────┐  ┌──────────┐  ┌──────────┐
│ Worker 1 │  │ Worker 2 │  │ Worker N │  ... (one per task, concurrent)
│ read_file│  │ read_file│  │ read_file│
│ bash     │  │ bash     │  │ bash     │
│ write    │  │ write    │  │ write    │
└────┬─────┘  └────┬─────┘  └────┬─────┘
     │              │              │
     └──────────────┼──────────────┘
                    │  post results
                    ▼
┌──────────────────────────────────────────────────┐
│  Orchestrator (reads all results, summarizes)    │
└──────────────────────────────────────────────────┘
```

**Flow:**
```
Input → Orchestrator explores + decomposes
           → N Workers execute concurrently (1 round)
               → Orchestrator summarizes → User
```

## How it works

1. **Orchestrator** receives the user request. It uses `bash`/`read_file` to quickly
   explore the project, then posts 2–5 concrete sub-tasks to the blackboard.
2. **Dynamic workers** — one per task — are created on the fly. Each gets its own
   `ToolRegistry` and runs on its own thread. All complete in a single round,
   with wall-clock time ≈ the slowest worker (not the sum of all).
3. **Orchestrator** reads every worker's results from the blackboard and synthesizes
   a concise summary for the user.

The **blackboard** is the only communication channel. Agents never call each
other directly — they read and write a shared, thread-safe, append-only log.

## Quick Start

```bash
git submodule update --init --recursive
mkdir build && cd build
cmake .. && make

# Configure
cp ../config.example.yaml ../config.yaml
# edit config.yaml — set openai.api_key

./swarmmind
```

## Configuration

`config.yaml` at the project root:

```yaml
openai:
  api_key: sk-your-key-here
  base_url: https://api.deepseek.com   # optional
  model: deepseek-chat                 # optional, default: gpt-4o
```

Precedence: `--model` flag > env vars > `config.yaml` > default.

## REPL Commands

| Command | Description |
|---------|-------------|
| `/exit`, `/quit` | Quit |
| `/clear` | Reset orchestrator history |
| `/blackboard` | Print all blackboard entries |

## Built-in Tools

| Tool | Who has it | Description |
|------|-----------|-------------|
| `read_file` | orchestrator, workers | Read a file |
| `write_file` | workers | Create or overwrite a file |
| `edit_file` | workers | Exact string replacement |
| `bash` | orchestrator, workers | Execute shell commands |
| `blackboard_post` | orchestrator, workers | Post to the shared blackboard |

## Dependencies

- C++17 compiler, CMake ≥ 3.14
- libcurl
- nlohmann/json (vendored, header-only)
- mini-yaml (vendored as git submodule)

## Project Structure

```
SwarmMind/
├── CMakeLists.txt
├── config.example.yaml
├── src/
│   ├── main.cpp                     # Entry point + REPL + orchestrator pipeline
│   ├── agent.h / agent.cpp          # Agent core loop + inline command parser
│   ├── llm_client.h / .cpp          # OpenAI-compatible SSE streaming (libcurl)
│   ├── blackboard.h / .cpp          # Shared blackboard + context formatter
│   ├── config.h / .cpp              # YAML config resolution
│   ├── console.h                    # io_mutex for concurrent terminal output
│   ├── inline_command.h / .cpp      # <cmd> stream parser (framework hook)
│   ├── tool.h / tool.cpp            # Tool base class + registry
│   ├── types.h                      # Message / ToolCall / StreamEvent types
│   └── tools/
│       ├── read_file.h / .cpp
│       ├── write_file.h / .cpp
│       ├── edit_file.h / .cpp
│       ├── bash.h / .cpp
│       └── blackboard_post.h / .cpp
├── third_party/
│   ├── nlohmann/                    # JSON (vendored header)
│   └── mini-yaml/                   # YAML (git submodule)
└── tests/
    ├── test_tools.cpp
    ├── test_blackboard.cpp
    └── test_inline_command.cpp
```

## License

MIT
