#include <base/component.h>
#include <util/construct_at.h>
#include <util/list.h>

#include "snapper.h"
#include "utils.h"

extern "C" void wait_for_continue();

/* Test Stats */
static unsigned total_tests = 0;
static unsigned successful_tests = 0;
static unsigned ignored_tests = 0;

/* Run Test */
[[maybe_unused]] static void
test (const char *name, bool condition)
{
  total_tests++;

  if (condition)
    {
      successful_tests++;
      Genode::log (GREEN "[PASS] ", name);
    }
  else
    {
      Genode::log (RED "[FAIL] ", name);
    }
}

#define TEST(condition) test (__PRETTY_FUNCTION__, condition)

/* Ignore Test */
[[maybe_unused]] static void
ignore (const char *name)
{
  ignored_tests++;
  Genode::log (YELLOW "[SKIP] ", name);
}

#define IGNORE                                                                \
  ignore (__PRETTY_FUNCTION__);                                               \
  return

/* Summarize Test Results */
void
summary (void)
{
  Genode::log ("\n", successful_tests, "/", total_tests, " tests passed.");
  Genode::log (ignored_tests, " tests were ignored.");
}

using namespace SnapperNS;

/* Tests */
void
test_snapshot_creation (void)
{
  bool init = snapper->init_snapshot () == Snapper::Ok;

  int mytestkey = 1;
  int mytestval = 5;
  bool snapshot
      = snapper->take_snapshot (&mytestval, sizeof (mytestval), mytestkey)
        == Snapper::Ok;

  bool commit = snapper->commit_snapshot () == Snapper::Ok;

  TEST (init && snapshot && commit);
}

void
test_successful_recovery (void)
{
  int vm_pages[1000] = { 0 };

  Genode::size_t size = sizeof (int);

  snapper->open_generation ();
  snapper->restore (&vm_pages, size, 1);
  snapper->close_generation ();

  Genode::log (vm_pages[0]);

  TEST (vm_pages[0] == 5);
}

void
test_unsuccessful_recovery (void)
{
  IGNORE;
  int vm_pages[1000];

  snapper->open_generation ();

  TODO ("try to recover into array");

  for (int i = 0; i < 1000; i++)
    {
      if (vm_pages[i] != i + 1)
        TEST (true);
    }

  TEST (false);
}

void
test_snapshot_purge (void)
{
  IGNORE;
  snapper->purge ();

  TEST (true);
}

void
Component::construct (Genode::Env &env)
{
  Genode::log ("-*- SNAPPER TEST SUITE -*-\n");

  Snapper::Config config;

#ifdef VERBOSE
  config.verbose = true;
#endif

  snapper = SnapperNS::Snapper::new_snapper (env, config);
  if (!snapper)
    Genode::error ("Could not initialize snapper object!");

  test_snapshot_creation ();
  test_successful_recovery ();
  test_unsuccessful_recovery ();
  test_snapshot_purge ();

  summary ();
  Genode::log ("\n-*- SNAPPER TESTS DONE -*-");
}
