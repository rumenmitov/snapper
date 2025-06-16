#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <os/path.h>
#include <os/vfs.h>
#include <rtc_session/connection.h>
#include <util/dictionary.h>
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
    static const Genode::uint8_t Version;
    
    enum State
    {
      Dormant,
      Creation,
      Restoration,
      Purge
    } state
        = Dormant;

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

    /**
     * @brief Saves the payload into a snapshot file and adds the
     *        mapping entry to the archive.
     */
    Result take_snapshot (void const *const, Genode::uint64_t, Genode::uint64_t);

    void
    commit_snapshot ()
    {
      TODO (__PRETTY_FUNCTION__);
    }

    /**
     * @brief Begin the restoration of a generation. If a generation is
     *        not specified, the latest one will be used.
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

    enum
    {
      SNAPPER_THRESH = 100
    };

    struct File
    {
      Genode::uint8_t version = 2;
      Genode::uint8_t rc = 0;
      Genode::uint32_t crc = 0;
      void *data = nullptr;
    };

    struct FilePath : Genode::Dictionary<FilePath, Genode::uint64_t>::Element
    {
      Genode::String<Vfs::MAX_PATH_LEN> value;

      FilePath (Genode::uint64_t id, const char *value,
                Genode::Dictionary<FilePath, Genode::uint64_t> &archive)
          : Element (archive, id), value (value)
      {
      }
    };

    static Snapper *instance;
    Genode::Env &env;

    Genode::Attached_rom_dataspace config;

    Genode::Heap heap;
    Genode::Root_directory snapper_root;
    Rtc::Connection rtc;

    Genode::Reconstructible<Genode::Directory> generation;
    Genode::Reconstructible<Genode::Directory> snapshot;
    Genode::String<Vfs::MAX_PATH_LEN> snapshot_dir_path;
    Genode::uint64_t snapshot_file_count = 0;

    Genode::Reconstructible<Genode::Dictionary<FilePath, Genode::uint64_t> >
        archive;
  };

} // namespace SnapperNS

#endif // __cplusplus

#endif // __SNAPPER_H
