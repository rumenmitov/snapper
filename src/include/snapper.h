#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>
#include <os/path.h>
#include <os/vfs.h>
#include <vfs/simple_env.h>
#include <vfs/types.h>

#include "utils.h"

namespace SnapperNS
{
  class Snapper;
  extern Snapper *snapper;

  class Snapper
  {
  public:
    enum State
    {
      Dormant,
      Creation,
      Restoration,
      Purge
    };

    enum Result
    {
      Ok,
      InvalidState,
      CouldNotCreateDir,
      CouldNotRemoveDir,
    };

    Snapper (Genode::Env &);

    static Snapper *new_snapper (Genode::Env &);

    /**
     * @brief Begin the snapshot process.
     */
    Result init_snapshot (void);

    void
    take_snapshot ()
    {
      TODO (__PRETTY_FUNCTION__);
    }

    void
    commit_snapshot ()
    {
      TODO (__PRETTY_FUNCTION__);
    }

    /**
     * @brief Begin the restoration of a generation. If a generation is
     * not specified, the latest one will be used.
     */
    void
    open_generation (const Genode::String<TIMESTAMP_STR_LEN> & = "")
    {
      TODO (__PRETTY_FUNCTION__);
    }

    void
    restore ()
    {
      TODO (__PRETTY_FUNCTION__);
    }

    void
    purge ()
    {
      TODO (__PRETTY_FUNCTION__);
    }

  private:
    Snapper (const Snapper &) = delete;
    Snapper operator= (Snapper &) = delete;

    static Snapper *instance;
    Genode::Env &env;

    Genode::Attached_rom_dataspace config;
    

    Genode::Heap heap;
    Genode::Root_directory snapper_root;
    Genode::Directory generation;
    Genode::Directory snapshot;

    State state = Dormant;
  };

} // namespace SnapperNS

#endif // __cplusplus

#endif // __SNAPPER_H
