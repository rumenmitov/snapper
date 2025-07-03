#include <base/component.h>
#include <base/log.h>

#include "snapper.h"

using namespace SnapperNS;

void
Component::construct (Genode::Env &env)
{
  Genode::log ("snapper init");

  snapper = SnapperNS::Snapper::new_snapper (env);
  if (!snapper)
    Genode::error ("could not initialize snapper object!");

  Genode::log ("snapper exit");
}
