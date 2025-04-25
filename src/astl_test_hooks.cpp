#include "astl/astl_test_hooks.h"

#include "astl_impl.hpp"

// Swaps out the current orchestrator's raw pointer and replaces it with a new raw pointer.
// For use only in tests, mostly tests covering the C -> C++ wrapper layer
ASTL_API astl_status_code astlInjectTestOrchestrator(astl_test_orchestrator_t  new_orchestrator,
                                                     astl_test_orchestrator_t* original_orchestrator) {
  if (!new_orchestrator || !original_orchestrator) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto& current_orchestrator = astl::Orchestrator::GetInstance();
  // caller of astlInjectTestOrchestrator is now responsible for managing the original orchestrator resources
  // most likely by swapping it back in when done
  *original_orchestrator = current_orchestrator.release();

  // cppcheck-suppress constVariablePointer
  auto* raw_orchestrator_ptr = static_cast<astl::Orchestrator*>(new_orchestrator);
  astl::Orchestrator::GetInstance().reset(raw_orchestrator_ptr);
  return ASTL_STATUS_SUCCESS;
}
