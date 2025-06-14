#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
#include <os/path.h>
#include <vfs/types.h>

#include "utils.h"

namespace SnapperNS {
class Snapper;
extern Snapper *snapper;

class Snapper {
public:
  Snapper(Genode::Env &env) : env(env) {}

  static Snapper *new_snapper(Genode::Env&);

  static Genode::Path<Vfs::MAX_PATH_LEN> snapper_root;

  void init_snapshot() {
    TODO(__PRETTY_FUNCTION__);
  }

  void take_snapshot() {
    TODO(__PRETTY_FUNCTION__);
  }

  void commit_snapshot() {
    TODO(__PRETTY_FUNCTION__);
  }

  /**
   * @brief Begin the restoration of a generation. If a generation is
   * not specified, the latest one will be used.
   */
  void open_generation(const Genode::String<TIMESTAMP_STR_LEN>& = "") 
  {
    TODO(__PRETTY_FUNCTION__);
  }

  void restore() {
    TODO(__PRETTY_FUNCTION__);
  }

  void purge() {
    TODO(__PRETTY_FUNCTION__);
  }

private:
  Snapper(const Snapper &) = delete;
  Snapper operator=(Snapper &) = delete;

  static Snapper *instance;
  Genode::Env &env;
};

} // namespace SnapperNS

#endif // __cplusplus

#endif // __SNAPPER_H
