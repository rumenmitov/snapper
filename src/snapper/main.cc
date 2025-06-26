#include <base/component.h>
#include <base/log.h>

#include "snapper.h"

using namespace SnapperNS;

void
Component::construct (Genode::Env &env)
{
  Genode::log ("snapper init");

  Snapper::Config config;

#ifdef VERBOSE
  config.verbose = true;
#endif

  snapper = SnapperNS::Snapper::new_snapper (env, config);
  if (!snapper)
    Genode::error ("could not initialize snapper object!");

  Genode::log ("snapper exit");
}
