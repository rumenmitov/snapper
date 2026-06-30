#include "snapper.h"

#include <base/heap.h>
#include <base/log.h>
#include <base/component.h>
#include <base/attached_rom_dataspace.h>


namespace Snapper { struct Main; }

struct Snapper::Main
{
  Genode::Env &env;
  
  Genode::Heap heap { env.ram(), env.rm() };
  Genode::Attached_rom_dataspace rom { env, "config" };

  /* TODO Should be in config struct! */
  Genode::Number_of_bytes bufsize { rom.node().attribute_value<Genode::Number_of_bytes>("bufsize", Genode::Number_of_bytes(1024 * 1024)) };
  
  Snapper::Root_component root { env, heap, bufsize };

  Main(Genode::Env &env) : env(env)
  {
    env.parent().announce(env.ep().manage(root));    
  }
};

void Component::construct(Genode::Env &env)
{
  static Snapper::Main main(env);
}

