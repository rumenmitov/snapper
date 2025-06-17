#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <os/path.h>
#include <os/vfs.h>
#include <rtc_session/connection.h>
#include <util/dictionary.h>
#include <util/noncopyable.h>
#include <vfs/simple_env.h>
#include <vfs/types.h>

#include "utils.h"

namespace SnapperNS
{
  class Snapper;
  extern Snapper *snapper;

  class Snapper : Genode::Noncopyable
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
      InitFailed,
      LoadGenFailed,
    };

    /**
     * @brief Keeps track of which files are backing up which virtual
     * object (identified by a ArchiveKey).
     */
    struct Archive : Genode::Noncopyable
    {
      typedef Genode::uint64_t ArchiveKey;
      struct ArchiveElement;
      typedef Genode::Dictionary<ArchiveElement, ArchiveKey> ArchiveContainer;

      /* INFO
       * No need to check if element is already present as this
       * constructor should ONLY be called by the with_element()'s no_match
       * function.
       */
      struct ArchiveElement
          : Genode::Dictionary<ArchiveElement, ArchiveKey>::Element
      {
        Genode::String<Vfs::MAX_PATH_LEN> value;

        ArchiveElement (
            ArchiveKey id, const char *value,
            Genode::Dictionary<ArchiveElement, ArchiveKey> &archive)
            : Element (archive, id), value (value)
        {
        }
      };

      Archive () : archive () {}

      ArchiveContainer archive;
    };

    ~Snapper ();

    /**
     * @brief Creates a new singleton of Snapper.
     */
    static Snapper *new_snapper (Genode::Env &);

    /**
     * @brief Begins the snapshot process.
     */
    Result init_snapshot (void);

    /**
     * @brief Saves the payload into a snapshot file and adds the
     *        mapping entry to the archive.
     */
    Result take_snapshot (void const *const, Genode::uint64_t,
                          Archive::ArchiveKey);

    /**
     * @brief Completes the snapshot process by saving the archiver's
     * contents into the archive file.
     */
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
    Snapper (Genode::Env &);
    Snapper (const Snapper &) = delete;
    Snapper operator= (Snapper &) = delete;

    enum CrashStates
    {
      SNAPSHOT_NOT_POSSIBLE,
      INVALID_ARCHIVE_FILE,
      INVALID_ARCHIVE_ENTRY,
    };

    enum
    {
      SNAPPER_THRESH = 100,
      SNAPPER_INTEGR  = true
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

    Genode::Reconstructible<Archive> archiver;

    /**
     * @brief Removes the last generation if it does not contain a
     * valid archive file (i.e. it is an incomplete snapshot).
     */
    Result __remove_unfinished_gen (void);

    /**
     * @brief Initializes the current generation by creating the
     * appropriate directories.
     */
    Result __init_gen (void);

    /**
     * @brief Tries to load the archive file from the specified
     * generation into archiver.
     *
     * If a generation is not specified, the latest generation will be used.
     */
    Result __load_gen (
        const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
            & = "");
  };

} // namespace SnapperNS

#endif // __cplusplus

#endif // __SNAPPER_H
