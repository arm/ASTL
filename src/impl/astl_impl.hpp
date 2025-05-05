#ifndef ASTL_API_IMPL_HPP_
#define ASTL_API_IMPL_HPP_

#include <memory>

#include "astl/astl.h"
#include "target.hpp"

static_assert(sizeof(astl_value_t) == sizeof(double),
              "astl_value_t union should not change size for ABI compatibility");

namespace astl {

class Orchestrator {
 public:
  static std::unique_ptr<Orchestrator>& GetInstance() {
    static auto instance = std::make_unique<Orchestrator>();
    return instance;
  }

  std::vector<std::unique_ptr<ITarget>>& GetTargets();
  void                                   SetTargets(std::vector<std::unique_ptr<ITarget>> targets);

  static astl_status_code Test();

 private:
  std::vector<std::unique_ptr<ITarget>> _targets;
};

}  // namespace astl

#endif  // ASTL_API_IMPL_HPP_
