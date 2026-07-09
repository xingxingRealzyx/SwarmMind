#pragma once

#include "../tool.h"
#include "../blackboard.h"

#include <memory>
#include <string>

namespace swarmmind {

class BlackboardPostTool : public Tool {
public:
    BlackboardPostTool(std::shared_ptr<Blackboard> blackboard, std::string author);

    std::string name() const override { return "blackboard_post"; }
    std::string description() const override;
    nlohmann::json parameters_schema() const override;
    std::string execute(const nlohmann::json& args) override;

private:
    std::shared_ptr<Blackboard> blackboard_;
    std::string author_;
};

} // namespace swarmmind
