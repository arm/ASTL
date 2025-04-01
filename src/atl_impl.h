#ifndef ATL_API_IMPL_H_
#define ATL_API_IMPL_H_

#include "atl/atl.h"

class AtlCollectorImplement {
public:
  atl_error_code Test();

private:
  bool test = true;
};

inline static auto &AtlCollectorInstance() {
  static AtlCollectorImplement atl_collector_instance{};
  return atl_collector_instance;
}

#endif // ATL_API_IMPL_H_
