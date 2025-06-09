#include "include/snapper.h"
#include <util/construct_at.h>

namespace Snapper
{
  Snapper* Snapper::instance = nullptr;
  
  Snapper::Snapper ()
  {
    if (instance)
      return;

    Genode::construct_at<Snapper*>(instance);
  }
}

// Local Variables:
// mode: c++
// End:
