#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <os/path.h>
#include <os/vfs.h>
#include <rtc_session/connection.h>
#include <util/attempt.h>
#include <util/dictionary.h>
#include <util/fifo.h>
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
    typedef Genode::uint32_t CRC;
    typedef Genode::uint8_t RC;
    typedef Genode::uint8_t VERSION;

    static const VERSION Version;

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
      InvalidVersion,
      NoMatches,
      NoData,
      RestoreFailed,
    };

    enum CrashStates
    {
      SNAPSHOT_NOT_POSSIBLE,
      INVALID_ARCHIVE_FILE,
      INVALID_ARCHIVE_ENTRY,
      INVALID_SNAPSHOT_FILE,
      REF_COUNT_FAILED,
      PURGE_FAILED,
    };

    /**
     * @brief Stores the configuration for the Snapper object.
     */
    struct Config
    {
      enum Default
      {
        _verbose = false,
        _redundancy = 3,
        _integrity = true,
        _threshold = 100,
        _max_snapshots = 0,
      };

      bool verbose = _verbose;
      Genode::uint64_t redundancy = _redundancy;
      bool integrity = _integrity;
      Genode::uint64_t threshold = _threshold;
      Genode::uint64_t max_snapshots = _max_snapshots;

    } snapper_config;

    /**
     * @brief Keeps track of which files are backing up which virtual
     * object (identified by a ArchiveKey).
     */
    struct Archive : Genode::Noncopyable
    {
      typedef Genode::uint64_t ArchiveKey;
      struct Backlink;
      struct ArchiveEntry;
      typedef Genode::Fifo<Backlink> Queue;
      typedef Genode::Dictionary<ArchiveEntry, ArchiveKey> ArchiveContainer;

      /**
       * @brief Represents a redundant snapshot file.
       */
      struct Backlink : Genode::Fifo<Backlink>::Element
      {
        Genode::String<Vfs::MAX_PATH_LEN> value;
        Backlink *_self;

        Backlink (const Genode::String<Vfs::MAX_PATH_LEN> &value)
            : value (value), _self (nullptr)
        {
        }

        enum Error
        {
          None,
          MissingFieldErr,
          InsufficientSizeErr,
          OpenErr,
          StatsErr,
          AllocErr,
          WriteErr,
        };

        Genode::Attempt<Snapper::VERSION, Error> get_version (void);
        Genode::Attempt<Snapper::CRC, Error> get_integrity (void);
        Genode::Attempt<Snapper::RC, Error> get_reference_count (void);
        Genode::Attempt<Genode::size_t, Error> get_data_size (void);
        Error get_data (Genode::Byte_range_ptr &);

        Genode::Attempt<Snapper::RC, Error>
        set_reference_count (const Snapper::RC);
      };

      /**
       * @brief Identifies which Backlinks belong to which ArchiveKey.
       */
      struct ArchiveEntry
          : Genode::Dictionary<ArchiveEntry, ArchiveKey>::Element
      {
        Queue queue;
        ArchiveEntry *_self;

        ArchiveEntry (ArchiveKey id,
                      Genode::Dictionary<ArchiveEntry, ArchiveKey> &archive)
            : Element (archive, id), queue (), _self (nullptr)
        {
        }

        ArchiveEntry (const ArchiveEntry &) = delete;
        bool operator= (const ArchiveEntry &) = delete;
      };

      Archive () : archive () {}
      ~Archive ();

      ArchiveContainer archive;

      /**
       * @brief Inserts entry into the archive. If the key is already
       *        present the entry is prepended to a FIFO queue.
       */
      void insert (const ArchiveKey,
                   const Genode::String<Vfs::MAX_PATH_LEN> &);

      /**
       * @brief Removes entry from archive.
       */
      void remove (const ArchiveKey);
    };

    Genode::Env &env;

    Genode::Attached_rom_dataspace config;

    Genode::Heap heap;
    Genode::Root_directory snapper_root;
    Rtc::Connection rtc;

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
     *        contents into the archive file.
     *
     * @throw SNAPSHOT_NOT_POSSIBLE
     */
    Result commit_snapshot (void);

    /**
     * @brief Begin the restoration of a generation. If a generation is
     *        not specified, the latest one will be used.
     */
    Result open_generation (
        const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
            & = "");

    /**
     * @brief Restore a snapshot file from an opened generation (see
     *        open_generation()). The buffer should be large enough to
     *        save the entire data section of the snapshot file.
     */
    Result restore (void *, Genode::size_t, Archive::ArchiveKey);

    /**
     * @brief End the restoration procedure.
     */
    Result close_generation (void);

    /**
     * @brief Remove a specified generation. If no generation is specified,
     * remove the oldest one.
     */
    Result
    purge (const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
               & = "");

    /**
     * @brief Purges expired generations.
     */
    void purge_expired (void);

  private:
    Snapper (Genode::Env &);
    Snapper (const Snapper &) = delete;
    Snapper operator= (Snapper &) = delete;

    enum
    {
      SNAPPER_THRESH = 100,
      SNAPPER_INTEGR = true
    };

    static Snapper *instance;

    Genode::Reconstructible<Genode::Directory> generation;
    Genode::Reconstructible<Genode::Directory> snapshot;
    Genode::String<Vfs::MAX_PATH_LEN> snapshot_dir_path;
    Genode::uint64_t snapshot_file_count = 0;
    Genode::uint64_t total_snapshot_objects = 0;

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
     * @brief Reset the Snapper state to defaults.
     */
    void __reset_gen (void);

    /**
     * @brief Tries to load the archive file from the specified
     * generation into archiver.
     *
     * If a generation is not specified, the latest generation will be used.
     */
    Result __load_gen (
        const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
            & = "");

    /**
     * @brief Aborts the snapshot by removing the snapshot and
     *        generation directories.
     */
    void __abort_snapshot (void);

    /**
     * @brief Updates the reference counts of all backlinks stored
     *        in the archiver. Should only be called after the archive file has
     *        been saved, else if the archive file cannot be saved there could
     *        be "zombie" snapshot files which will never be removed from the
     *        file-system!
     */
    void __update_references (void);
  };

} // namespace SnapperNS

#endif // __cplusplus

#endif // __SNAPPER_H
