#ifndef INCLUDE_ASTL_TEST_HOOKS_H_
#define INCLUDE_ASTL_TEST_HOOKS_H_

#include "astl/astl_errors.h"
#include "astl/astl_utils.h"

typedef void*             astl_test_orchestrator_t;
ASTL_API astl_status_code astlInjectTestOrchestrator(astl_test_orchestrator_t  new_orchestrator,
                                                     astl_test_orchestrator_t* original_orchestrator);

#endif  // INCLUDE_ASTL_TEST_HOOKS_H_
