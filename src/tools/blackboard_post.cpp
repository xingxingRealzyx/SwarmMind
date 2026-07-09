#include "blackboard_post.h"

namespace swarmmind {

BlackboardPostTool::BlackboardPostTool(std::shared_ptr<Blackboard> blackboard,
                                       std::string author)
    : blackboard_(std::move(blackboard))
    , author_(std::move(author))
{
}

std::string BlackboardPostTool::description() const {
    return "Post an entry to the shared blackboard. Other agents can read it. "
           "Use type 'task' for sub-tasks (tag with 'pending'), 'claim' when "
           "picking up a task, 'result' when done, or 'observation'/'question'/"
           "'conclusion' for general communication.";
}

nlohmann::json BlackboardPostTool::parameters_schema() const {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"type", {{"type", "string"}, {"description", "Entry type: task, claim, result, observation, question, conclusion"}}},
            {"content", {{"type", "string"}, {"description", "The content to post"}}},
            {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional tags for filtering (e.g. ['pending', 'code-review'])"}}},
        }},
        {"required", {"type", "content"}},
    };
}

std::string BlackboardPostTool::execute(const nlohmann::json& args) {
    std::string type = args.value("type", "note");
    std::string content = args.value("content", "");

    if (content.empty()) {
        return R"({"success": false, "error": "content is required"})";
    }

    std::vector<std::string> tags;
    if (args.contains("tags") && args["tags"].is_array()) {
        for (const auto& t : args["tags"]) {
            if (t.is_string()) tags.push_back(t.get<std::string>());
        }
    }

    blackboard_->post(author_, type, content, tags);
    return R"({"success": true})";
}

} // namespace swarmmind
