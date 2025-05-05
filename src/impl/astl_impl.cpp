#include "astl_impl.hpp"

#include "astl_logger.hpp"

namespace astl {

std::vector<std::unique_ptr<ITarget>>& Orchestrator::GetTargets() { return _targets; }

void Orchestrator::SetTargets(std::vector<std::unique_ptr<ITarget>> targets) { _targets = std::move(targets); }

astl_status_code Orchestrator::Test() {
  ASTL_LOG_INFO("Test method is deprecated: {:d}", static_cast<uint32_t>(ASTL_STATUS_DEPRECATED_API));
  return ASTL_STATUS_DEPRECATED_API;
}

}  // namespace astl
