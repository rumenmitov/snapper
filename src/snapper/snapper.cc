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

    Genode::construct_at<Snapper> (Snapper::instance);

    Genode::log("new snapper created.");
    return Snapper::instance;
  }
}
