#ifndef ASTL_API_IMPL_H_
#define ASTL_API_IMPL_H_

#include "astl/astl.h"

static_assert(sizeof(astl_value_t) == sizeof(double),
              "astl_value_t union should not change size for ABI compatibility");

namespace astl {
class CollectorImplement {
 public:
  astl_error_code Test();

 private:
  bool test = true;
};

inline static auto &CollectorInstance() {
  static CollectorImplement astl_collector_instance{};
  return astl_collector_instance;
}

}  // namespace astl

#endif  // ASTL_API_IMPL_H_
