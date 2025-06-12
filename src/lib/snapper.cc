#include <util/construct_at.h>

#include "snapper.h"


namespace SnapperNS
{
  Snapper *snapper = nullptr;
  Snapper *Snapper::instance = nullptr;
  Genode::Path<Vfs::MAX_PATH_LEN> Snapper::snapper_root = "/snapper";

  Snapper *
  Snapper::new_snapper (Genode::Env& env)
  {
    if (Snapper::instance)
      return Snapper::instance;

    static Snapper local_snapper(env);
    env.exec_static_constructors();
    
    Snapper::instance = &local_snapper;

#ifdef VERBOSE
    Genode::log("new snapper created");
#endif // VERBOSE

    return Snapper::instance;
  }
}
