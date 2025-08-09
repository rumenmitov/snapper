#include <base/component.h>
#include <base/log.h>

#include "snapper.h"

using namespace Snapper;

void
Component::construct (Genode::Env &env)
{
  Snapper::Main snapper(env);
  env.parent().exit(0);
}
