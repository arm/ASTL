#include "atl/atl.h"

#include "atl_impl.h"

atl_error_code atlTest() {
  atl_error_code result = AtlCollectorInstance().Test();
  return result;
}
