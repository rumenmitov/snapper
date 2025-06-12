#include <base/component.h>
#include <base/log.h>

#include "snapper.h"

using namespace SnapperNS;

void Component::construct(Genode::Env& env) 
{
  Genode::log("snapper init");

  snapper = Snapper::new_snapper(env);
  
  Genode::log("snapper exit");
}


