#include <base/component.h>
#include <base/log.h>

void Component::construct(Genode::Env& ) 
{
  Genode::log("snapper init");
  Genode::log("snapper exit");
}


