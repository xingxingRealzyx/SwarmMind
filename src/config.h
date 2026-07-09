#pragma once

#include <string>

namespace swarmmind {

// Resolved runtime configuration for the LLM client.
struct Config {
    std::string api_key;
    std::string base_url;
    std::string model;
};

// Resolves the final config by precedence: --model flag > env vars >
// config.yaml > built-in default ("gpt-4o").
//
// config.yaml defaults to one level above the binary's directory — since
// the binary lives in build/, that's the project root's config.yaml —
// unless explicit_config_path is non-empty, in which case that path is
// used instead.
//
// argv0 locates the binary for the default path above. model_override is
// the --model CLI flag value (empty if not given).
//
// Exits the process with an error message if --config points to a
// nonexistent file, or if no API key can be found anywhere.
Config resolve_config(const char* argv0,
                      const std::string& explicit_config_path,
                      const std::string& model_override);

} // namespace swarmmind
