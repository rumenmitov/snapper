#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <os/path.h>
#include <os/vfs.h>
#include <root/component.h>
#include <rtc_session/connection.h>
#include <timer_session/connection.h>
#include <util/attempt.h>
#include <util/dictionary.h>
#include <util/fifo.h>
#include <util/noncopyable.h>
#include <vfs/simple_env.h>
#include <vfs/types.h>

namespace Snapper
{
  class Main;
  struct Archive;
  struct Backlink;

  typedef Genode::uint32_t CRC;
  typedef Genode::uint8_t RC;
  typedef Genode::uint8_t VERSION;

  enum
  {
    Version = 2
  };

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
    NoPriorGen,
    InvalidState,
    InitFailed,
    LoadGenFailed,
    InvalidVersion,
    IntegrityFailed,
    NoMatches,
    NoData,
    RestoreFailed,
    PurgeDenied,
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
   * @field verbose 					Whether to print
   *                          verbose output.
   * @field redundancy				After reaching this reference
   *                          count, a redundant file copy will be
   *                          created for subsequent snapshot.
   * @field integrity 				If true, crash the system on
   *                          failed integrity checks, otherwise log a warning.
   * @field threshold 				The maximum number of files in
   *                          a snapshot sub-directory.
   * @field max_snapshots	  	The maximum number of complete
   *                          snapshots inside <snapper-root>.
   * @field min_snapshots	  	The minimum number of generations that
   *                          need to be present for a purge to be
   *                          possible.
   * @field expiration		  	How many seconds a generation should
   *                          be kept.
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
      _min_snapshots = 0,
      _expiration = 0,
    };

    bool verbose = _verbose;
    Genode::uint64_t redundancy = _redundancy;
    bool integrity = _integrity;
    Genode::uint64_t threshold = _threshold;
    Genode::uint64_t max_snapshots = _max_snapshots;
    Genode::uint64_t min_snapshots = _min_snapshots;
    Genode::uint64_t expiration = _expiration;
  };

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

      // INFO The backlink is allocated by the Archive's allocator,
      // hence we need to store the pointer somewhere for later deallocation.
      Backlink *_self;

      Genode::Heap &heap;
      Genode::Directory &snapper_root;
      bool verbose;

      Backlink () = delete;
      Backlink (Genode::Heap &heap, Genode::Directory &snapper_root,
                bool verbose, const Genode::String<Vfs::MAX_PATH_LEN> &value)
          : value (value), _self (nullptr), heap (heap),
            snapper_root (snapper_root), verbose (verbose)
      {
      }

      enum Error
      {
        None,
        InvalidVersion,
        InvalidIntegrity,
        MissingFieldErr,
        InsufficientSizeErr,
        OpenErr,
        StatsErr,
        AllocErr,
        WriteErr,
      };

      /**
       * @brief Get the version of the backlink.
       */
      Genode::Attempt<Snapper::VERSION, Error> get_version (void);

      /**
       * @brief Get the integrity of the backlink.
       */
      Genode::Attempt<Snapper::CRC, Error> get_integrity (void);

      /**
       * @brief Get the reference count of the backlink.
       */
      Genode::Attempt<Snapper::RC, Error> get_reference_count (void);

      /**
       * @brief Get the size of the data stored in the backlink.
       */
      Genode::Attempt<Genode::size_t, Error> get_data_size (void);

      /**
       * @brief Get the data stored in the backlink.
       */
      Error get_data (Genode::Byte_range_ptr &);

      /**
       * @brief Update the reference count of the backlink.
       */
      Genode::Attempt<Snapper::RC, Error>
      set_reference_count (const Snapper::RC);

      /**
       * @brief Checks if the backlink's version and CRC are valid.
       */
      bool is_backlink_valid (Snapper::CRC);
    };

    /**
     * @brief Identifies which Backlinks belong to which ArchiveKey.
     */
    struct ArchiveEntry : Genode::Dictionary<ArchiveEntry, ArchiveKey>::Element
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

    Archive () = delete;

    /**
     * @brief Constructs a new Archive to keep track of backlink
     * mappings.
     * @throws Genode::Create_failed
     */
    Archive (Genode::Heap &, Genode::Directory &, bool);

    ~Archive ();

    ArchiveContainer archive;

    Genode::Heap &heap;
    Genode::Directory &snapper_root;
    bool verbose;

    Genode::uint64_t total_backlinks = 0;

    /**
     * @brief Inserts entry into the archive. If the key is already
     *        present the entry is prepended to a FIFO queue.
     */
    void insert (const ArchiveKey, const Genode::String<Vfs::MAX_PATH_LEN> &);

    /**
     * @brief Saves the archive structure to a file in the specified
     * directory. Prepends the Snapper version and the CRC of the
     * archive structure.
     */
    void commit (Genode::Directory &,
                 const Genode::String<Vfs::MAX_PATH_LEN> & = "archive");

    /**
     * @brief Removes entry from archive.
     */
    void remove (const ArchiveKey);

    /**
     * @brief Reads the archive file provided, and inserts the data
     * into the existing Archive structure.
     * @throws Snapper::CrashStates
     */
    void extract_from_archive_file (const Genode::Readonly_file &);
  };

  class Main : Genode::Noncopyable
  {
  public:
    Main () = delete;
    Main (Genode::Env &env);
    ~Main ();

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

    Genode::Attached_rom_dataspace rom;

    Genode::Heap heap;
    Genode::Root_directory snapper_root;
    Rtc::Connection rtc;
    Timer::Connection timer;

    Config config;

  private:
    State state = Dormant;

    Genode::Reconstructible<Genode::Directory> generation;
    Genode::Reconstructible<Genode::Directory> snapshot;
    Genode::String<Vfs::MAX_PATH_LEN> snapshot_dir_path;
    Genode::uint64_t snapshot_file_count = 0;

    Genode::Reconstructible<Archive> archiver;

    /**
     * @brief Checks if archive file exists and has a valid CRC.
     */
    bool __valid_archive (const Genode::Path<Vfs::MAX_PATH_LEN> &);

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

    /**
     * @brief Returns the number of valid generations.
     */
    Genode::uint64_t __num_gen (void);

    /**
     * @brief Returns the number of directory entries in the
     * 				directory. This is needed because the
     *        num_dirent() method is unreliable.
     */
    Genode::uint64_t __num_dirent (const Genode::String<Vfs::MAX_PATH_LEN> &);

    /**
     * @brief Deletes the target. If the target is the last remaining
     *        dirent, it deletes parent directory. This continues recursively
     *        until <snapper-root>.
     */
    void __delete_upwards (const char *);
  };

} // namespace Snapper

#endif // __cplusplus

#endif // __SNAPPER_H
