#include <base/component.h>
#include <base/log.h>

#include "snapper.h"

using namespace SnapperNS;

void
Component::construct (Genode::Env &env)
{
  snapper = SnapperNS::Snapper::new_snapper (env);
  if (!snapper)
    Genode::error ("could not initialize snapper object!");

  env.parent().exit(0);
}
