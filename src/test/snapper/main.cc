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

#define TEST(condition) return test (__PRETTY_FUNCTION__, condition)

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
  if (snapper->init_snapshot () != Snapper::Ok)
    {
      TEST (false);
    }

  for (int i = 1; i <= TESTS; i++)
    {
      if (snapper->take_snapshot (&i, sizeof (decltype (i)), i) != Snapper::Ok)
        {
          TEST (false);
        }
    }

  if (snapper->commit_snapshot () != Snapper::Ok)
    {
      TEST (false);
    }

  TEST (true);
}

void
test_successful_recovery (void)
{
  int value = 0;
  Genode::size_t size = sizeof (decltype (value));

  if (snapper->open_generation () != Snapper::Ok)
    TEST (false);

  for (int i = 1; i <= TESTS; i++)
    {
      snapper->restore (&value, size, i);
      if (value != i)
        TEST (false);
    }

  if (snapper->close_generation () != Snapper::Ok)
    TEST (false);

  TEST (true);
}

void
test_snapshot_purge (void)
{
  bool ok = snapper->purge () == Snapper::Ok;

  TEST (ok);
}

void
Component::construct (Genode::Env &env)
{
  Genode::log ("-*- SNAPPER TEST SUITE -*-\n");

  snapper = SnapperNS::Snapper::new_snapper (env);
  if (!snapper)
    Genode::error ("could not initialize snapper object!");

  test_snapshot_creation ();
  test_successful_recovery ();
  test_snapshot_purge ();

  summary ();
  Genode::log ("\n-*- SNAPPER TESTS DONE -*-");

  env.parent ().exit ((successful_tests == total_tests) ? 0 : 1);
}
