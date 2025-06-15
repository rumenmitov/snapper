#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <os/path.h>
#include <os/vfs.h>
#include <rtc_session/connection.h>
#include <vfs/simple_env.h>
#include <vfs/types.h>
#include <util/dictionary.h>

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
    open_generation (const Genode::String<
                         Vfs::Directory_service::Dirent::Name::MAX_LEN> & = "")
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

    struct File 
    {
      Genode::uint8_t version = 2;
      Genode::uint8_t rc      = 0;
      Genode::uint32_t crc    = 0;
      void *data              = nullptr;
    };

    static Snapper *instance;
    Genode::Env &env;

    Genode::Attached_rom_dataspace config;

    Genode::Heap heap;
    Genode::Root_directory snapper_root;
    Rtc::Connection rtc;

    Genode::Reconstructible<Genode::Directory> generation;
    Genode::Reconstructible<Genode::Directory> snapshot;

    State state = Dormant;
    Genode::Reconstructible<Genode::Dictionary<File, const char *>> archive;
  };

} // namespace SnapperNS

#endif // __cplusplus

#endif // __SNAPPER_H
