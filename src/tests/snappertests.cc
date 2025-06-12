#include <base/component.h>
#include <util/construct_at.h>
#include <util/list.h>

#include "snapper.h"
#include "utils.h"

/* Test Stats */
static unsigned total_tests = 0;
static unsigned successful_tests = 0;
static unsigned ignored_tests = 0;

/* Run Test */
[[maybe_unused]] static void test(const char *name, bool condition) {
  total_tests++;

  if (condition) {
    successful_tests++;
    Genode::log(GREEN "[PASS] ", name);
  } else {
    Genode::log(RED "[FAIL] ", name);
  }
}

#define TEST(condition) test(__PRETTY_FUNCTION__, condition)

/* Ignore Test */
[[maybe_unused]] static void ignore(const char *name) {
  ignored_tests++;
  Genode::log(YELLOW "[SKIP] ", name);
}

#define IGNORE                                                                 \
  ignore(__PRETTY_FUNCTION__);                                                 \
  return

/* Summarize Test Results */
void summary(void) {
  Genode::log("\n", successful_tests, "/", total_tests, " tests passed.");
  Genode::log(ignored_tests, " tests were ignored.");
}

using namespace SnapperNS;

/* Tests */
void test_snapshot_creation(void) {
  IGNORE;

  TODO("take snapshot of pages");

  TEST(true);
}

void test_successful_recovery_1(void) 
{
  IGNORE;
  TODO("recover into array and check array values");
}

void Component::construct(Genode::Env &env) {
  Genode::log("-*- SNAPPER TEST SUITE -*-\n");

  snapper = SnapperNS::Snapper::new_snapper(env);
  if (!snapper) Genode::error("Could not initialize snapper object!");

  test_snapshot_creation();
  test_successful_recovery_1();

  summary();
  Genode::log("\n-*- SNAPPER TESTS DONE -*-");
}
