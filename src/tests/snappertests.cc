#include <base/component.h>

#include "snapper.h"

/* INFO __PRETTY_FUNCTION__ is only available on gcc and clang. */
#ifndef __PRETTY_FUNCTION__
#define __PRETTY_FUNCTION__ "Test"
#endif // __PRETTY_FUNCTION__

/* Colors */
#define RED "\033[31m"
#define GREEN "\033[32m"
#define RESET "\033[0m"

/* Assert */
#define ASSERT(condition)                                                      \
  {                                                                            \
    if (condition) {                                                           \
      Genode::log(GREEN __PRETTY_FUNCTION__ " passed!" RESET);                 \
    } else {                                                                   \
      Genode::log(RED __PRETTY_FUNCTION__ " failed!" RESET);                   \
    }                                                                          \
  }

using namespace SnapperNS;

void test_initialization(void) 
{
  snapper = Snapper::new_snapper();
  ASSERT(snapper);
}

void Component::construct(Genode::Env &) 
{
  Genode::log("-*- SNAPPER TEST SUITE -*-");
  test_initialization();
  Genode::log("-*- SNAPPER TESTS DONE -*-");
}
