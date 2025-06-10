#include <base/component.h>
#include <base/log.h>

#include "include/snapper.h"

using namespace SnapperNS;

void Component::construct(Genode::Env& ) 
{
  Genode::log("snapper init");

  snapper = Snapper::new_snapper();
  
  Genode::log("snapper exit");
}


