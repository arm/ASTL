#include "astl_impl.hpp"

namespace astl {

std::vector<std::unique_ptr<ITarget>>& Orchestrator::GetTargets() { return _targets; }

void Orchestrator::SetTargets(std::vector<std::unique_ptr<ITarget>> targets) { _targets = std::move(targets); }

astl_status_code Orchestrator::Test() { return ASTL_STATUS_DEPRECATED_API; }

}  // namespace astl
