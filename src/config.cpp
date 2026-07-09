#include "config.h"

#include <Yaml.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace swarmmind {

namespace {

std::string dirname_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    return pos == std::string::npos ? "." : path.substr(0, pos);
}

bool file_exists(const std::string& path) {
    std::ifstream ifs(path);
    return ifs.good();
}

// Reads config.yaml under an "openai:" key:
//   openai:
//     api_key: sk-...
//     base_url: https://api.deepseek.com
//     model: deepseek-chat
// Missing keys are left untouched in cfg (caller applies fallbacks).
void load_config_file(const std::string& path, Config& cfg) {
    Yaml::Node root;
    try {
        Yaml::Parse(root, path.c_str());
    } catch (const Yaml::Exception& e) {
        std::cerr << "\033[33m[config: failed to parse " << path << ": " << e.what() << "]\033[0m\n";
        return;
    }

    Yaml::Node& openai = root["openai"];
    auto api_key = openai["api_key"].As<std::string>("");
    auto base_url = openai["base_url"].As<std::string>("");
    auto model = openai["model"].As<std::string>("");

    if (!api_key.empty()) cfg.api_key = api_key;
    if (!base_url.empty()) cfg.base_url = base_url;
    if (!model.empty()) cfg.model = model;
}

// Default location: one level above the binary's directory. The binary
// always lives in build/, so this resolves to the project root's
// config.yaml — a single, predictable default rather than a search.
Config load_default_config(const char* argv0, const std::string& explicit_path) {
    Config cfg;

    if (!explicit_path.empty()) {
        if (!file_exists(explicit_path)) {
            std::cerr << "Error: --config file not found: " << explicit_path << "\n";
            std::exit(1);
        }
        load_config_file(explicit_path, cfg);
        return cfg;
    }

    std::string default_path = dirname_of(argv0) + "/../config.yaml";
    if (file_exists(default_path)) {
        load_config_file(default_path, cfg);
    }
    return cfg;
}

} // namespace

Config resolve_config(const char* argv0,
                      const std::string& explicit_config_path,
                      const std::string& model_override) {
    Config cfg = load_default_config(argv0, explicit_config_path);

    // Env vars, if set, take priority over config.yaml
    const char* api_key_env = std::getenv("OPENAI_API_KEY");
    const char* base_url_env = std::getenv("OPENAI_BASE_URL");
    const char* model_env = std::getenv("OPENAI_MODEL");

    if (api_key_env && *api_key_env) cfg.api_key = api_key_env;
    if (base_url_env && *base_url_env) cfg.base_url = base_url_env;
    if (model_env && *model_env) cfg.model = model_env;

    if (!model_override.empty()) {
        cfg.model = model_override;
    } else if (cfg.model.empty()) {
        cfg.model = "gpt-4o";
    }

    if (cfg.api_key.empty()) {
        std::cerr << "Error: no API key found. Set openai.api_key in config.yaml "
                      "or the OPENAI_API_KEY environment variable.\n";
        std::exit(1);
    }

    return cfg;
}

} // namespace swarmmind
