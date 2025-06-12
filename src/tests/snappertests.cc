#include <base/component.h>

#include "snapper.h"

/* INFO __PRETTY_FUNCTION__ is only available on gcc and clang. */
// #ifndef __PRETTY_FUNCTION__
// #define __PRETTY_FUNCTION__ "Test"
// #endif // __PRETTY_FUNCTION__

/* Colors */
#define RED "\033[31m"
#define GREEN "\033[32m"
#define RESET "\033[0m"

/* Test Stats */
static unsigned total_tests = 0;
static unsigned successful_tests = 0;

/* Run Test */
void test(const char *name, bool condition) {
  total_tests++;
  
  if (condition) {
    successful_tests++;
    Genode::log(GREEN, name, " passed!" RESET);
  } else {
    Genode::log(RED, name, " failed!" RESET);
  }
}

/* Summarize Test Results */
void summary(void) 
{
  Genode::log("\n", successful_tests, "/", total_tests, " tests passed.");
}

using namespace SnapperNS;

void test_initialization(void) {
  snapper = Snapper::new_snapper();
  test(__PRETTY_FUNCTION__, snapper);
}

void Component::construct(Genode::Env &) {
  Genode::log("-*- SNAPPER TEST SUITE -*-\n");

  test_initialization();

  summary();
  Genode::log("\n-*- SNAPPER TESTS DONE -*-");
}
