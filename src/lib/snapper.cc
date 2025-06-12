#include <util/construct_at.h>

#include "snapper.h"


namespace SnapperNS
{
  Snapper *snapper = nullptr;
  Snapper *Snapper::instance = nullptr;

  Snapper *
  Snapper::new_snapper (void)
  {
    if (Snapper::instance)
      return Snapper::instance;

    static Snapper local_snapper;
    Snapper::instance = &local_snapper;

#ifdef VERBOSE
    Genode::log("new snapper created");
#endif // VERBOSE

    return Snapper::instance;
  }
}
