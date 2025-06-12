#include <base/component.h>
#include <util/construct_at.h>
#include <util/list.h>

#include "snapper.h"

/* INFO __PRETTY_FUNCTION__ is only available on gcc and clang. */
// #ifndef __PRETTY_FUNCTION__
// #define __PRETTY_FUNCTION__ "Test"
// #endif // __PRETTY_FUNCTION__

/* Colors */
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define RESET "\033[0m"

/* Test Stats */
static unsigned total_tests = 0;
static unsigned successful_tests = 0;
static unsigned ignored_tests = 0;

/* Run Test */
static void test(const char *name, bool condition) {
  total_tests++;

  if (condition) {
    successful_tests++;
    Genode::log(GREEN, name, " passed!" RESET);
  } else {
    Genode::log(RED, name, " failed!" RESET);
  }
}

#define TEST(condition) test(__PRETTY_FUNCTION__, condition)

/* Ignore Test */
[[maybe_unused]] static void ignore(const char *name) {
  ignored_tests++;
  Genode::log(YELLOW, name, " ignored!" RESET);
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

struct IntElement : Genode::List<IntElement>::Element {
  int value;
  IntElement(int value) : value(value) {}
};

/* Tests */
void test_initialization(void) {
  snapper = Snapper::new_snapper();
  TEST(snapper);
}

void test_snapshot_creation(void) {
  //IGNORE;

  snapper = SnapperNS::Snapper::new_snapper();

  if (!snapper)
    test(__PRETTY_FUNCTION__, false);

  Genode::List<IntElement> linked_list;
  for (int i = 1; i <= 1000; i++) {
    IntElement *new_element = new IntElement(i);

    linked_list.insert(new_element);
  }

  int counter = 1;
  
  for (IntElement *element = linked_list.first(); element; element = element->next()) {
    if (element->value != counter++) TEST(false);
  }

  TEST(true);
}

void Component::construct(Genode::Env &) {
  Genode::log("-*- SNAPPER TEST SUITE -*-\n");

  test_initialization();
  test_snapshot_creation();

  summary();
  Genode::log("\n-*- SNAPPER TESTS DONE -*-");
}
