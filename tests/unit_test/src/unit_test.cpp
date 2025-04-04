#include "astl_impl.h"

// Simple placeholder for an executable demonstrating linking to the astl_static library
// and accessing its internal classes for easy testing.
// TODO - replace with a real unit test framework
int main(int argc, char** argv) {
  auto status = astl::CollectorInstance().Test();
  return 0;
}