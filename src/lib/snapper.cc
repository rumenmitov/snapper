#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"
#include "snapper_session/snapper_session.h"
#include "utils.h"

namespace Snapper
{
  /*
   * CONSTRUCTORS
   */

  Main::Main (Genode::Env &env)
      : rom (env, "config"), heap (env.ram (), env.rm ()),
        snapper_root (env, heap, rom.xml ().sub_node ("vfs")), rtc (env),
        timer (env), config (),
        generation (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot_dir_path ("/"), archiver (*this)
  {
    config.verbose
        = rom.xml ().attribute_value<decltype (Snapper::Config::verbose)> (
            "verbose", Snapper::Config::_verbose);

    config.redundancy
        = rom.xml ().attribute_value<decltype (Snapper::Config::redundancy)> (
            "redundancy", Snapper::Config::_redundancy);

    config.integrity
        = rom.xml ().attribute_value<decltype (Snapper::Config::integrity)> (
            "integrity", Snapper::Config::_integrity);

    config.threshold
        = rom.xml ().attribute_value<decltype (Snapper::Config::threshold)> (
            "threshold", Snapper::Config::_threshold);

    config.max_snapshots
        = rom.xml ()
              .attribute_value<decltype (Snapper::Config::max_snapshots)> (
                  "max_snapshots", Snapper::Config::_max_snapshots);

    config.min_snapshots
        = rom.xml ()
              .attribute_value<decltype (Snapper::Config::min_snapshots)> (
                  "min_snapshots", Snapper::Config::_min_snapshots);

    config.expiration
        = rom.xml ().attribute_value<decltype (Snapper::Config::expiration)> (
            "expiration", Snapper::Config::_expiration);

    static Snapper::Root_component root (env, env.ep (), heap, *this);
    env.parent ().announce (env.ep ().manage (root));
  }

  Main::~Main ()
  {
    generation.destruct ();
    snapshot.destruct ();
    archiver.destruct ();

    if (config.verbose)
      Genode::log ("snapper object destroyed.");
  }

  /*
   * CREATING SNAPSHOT
   */
  Snapper::Result
  Main::init_snapshot ()
  {
    if (state != Dormant)
      return InvalidState;

    state = Creation;
    snapshot_file_count = 0;

    Result res = __remove_unfinished_gen ();
    if (res != Ok)
      return res;

    res = __init_gen ();
    if (res != Ok)
      return res;

    return res;
  }

  Snapper::Result
  Main::take_snapshot (void const *const payload, Genode::uint64_t size,
                       Archive::ArchiveKey identifier)
  {
    if (state != Creation)
      return InvalidState;

    bool new_backlink_needed = false;
    Snapper::CRC crc = crc32 (payload, size);

    // check if identifier exists in the mapping and if the crc
    // matches the calculated crc of the payload.
    archiver->archive.with_element (
        identifier,
        [this, &new_backlink_needed, &crc] (Archive::ArchiveEntry &entry) {
          /* INFO
             Only need to check the latest backlink as it is assumed
             that all backlinks in the queue store the same data as
             part of the redundant policy.
           */
          entry.queue.head ([this, &entry, &new_backlink_needed,
                             &crc] (Archive::Backlink &backlink) {
            if (!backlink.is_backlink_valid (crc))
              {
                // INFO Remove all the previous backlinks since they
                // no longer correspond to the data.
                while (!entry.queue.empty ())
                  {
                    entry.queue.dequeue ([this] (Archive::Backlink &backlink) {
                      Genode::destroy (heap, backlink._self);
                    });
                  }

                return;
              }

            backlink.get_reference_count ().with_result (
                [this, &backlink, &new_backlink_needed] (Snapper::RC rc) {
                  if (rc >= config.redundancy)
                    {
                      if (config.verbose)
                        Genode::log ("backlink reference count exceeded: ",
                                     backlink.value,
                                     ". Creating redundant copy.");

                      new_backlink_needed = true;
                    }
                },
                [this, &entry,
                 &new_backlink_needed] (Snapper::Archive::Backlink::Error) {
                  new_backlink_needed = true;

                  while (!entry.queue.empty ())
                    {
                      entry.queue.dequeue (
                          [this] (Archive::Backlink &backlink) {
                            Genode::destroy (heap, backlink._self);
                          });
                    }
                });
          });
        },
        [&new_backlink_needed] () { new_backlink_needed = true; });

    if (!new_backlink_needed)
      return Ok;

    // create a new snapshot file and write to it the payload metadata
    // and the payload data

    snapshot_file_count++;

    if (snapshot_file_count >= config.threshold)
      {
        snapshot->create_sub_directory ("ext");
        if (!snapshot->directory_exists ("ext"))
          {
            Genode::error ("could not create extender sub-directory!");
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        snapshot.construct (*snapshot, "ext");
        snapshot_dir_path = Genode::Directory::join (snapshot_dir_path, "ext");
        snapshot_file_count = 0;
      }

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
        filepath_base ((Genode::Hex (snapshot_file_count)));

    try
      {
        VERSION ver = Version;

        Genode::New_file file (*snapshot, filepath_base);

        Genode::size_t buf_size = sizeof (Snapper::VERSION)
                                  + sizeof (Snapper::CRC)
                                  + sizeof (Snapper::RC) + size;

        char *buf = new (heap) char[buf_size];

        Genode::memcpy (buf, (char *)&ver, sizeof (Snapper::VERSION));
        Genode::memcpy (buf + sizeof (Snapper::VERSION), (char *)&crc,
                        sizeof (Snapper::CRC));

        /* INFO
         The reference count will be incremented when the full
         snapshot is committed. It starts at 0, because the snapshot
         file is currently not referenced by any archive file.
        */
        Snapper::RC reference_count = 0;
        Genode::memcpy (buf + sizeof (Snapper::VERSION)
                            + sizeof (Snapper::CRC),
                        (char *)&reference_count, sizeof (Snapper::RC));

        Genode::memcpy (buf + sizeof (Snapper::VERSION) + sizeof (Snapper::CRC)
                            + sizeof (Snapper::RC),
                        payload, size);

        Genode::New_file::Append_result res = file.append (buf, buf_size);

        heap.free (buf, buf_size);

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("could not write to backlink file: ",
                           filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }
      }
    catch (Genode::New_file::Create_failed)
      {
        Genode::error ("could not create file: ", filepath_base);
        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }

    // save the snapshot file's path (i.e. a backlink) into the archive
    // (relative to snapper_root)

    archiver->insert (identifier, Genode::Directory::join (snapshot_dir_path,
                                                           filepath_base));
    return Ok;
  }

  Snapper::Result
  Main::commit_snapshot (void)
  {
    if (state != Creation)
      return InvalidState;

    if (!generation.constructed ())
      {
        Genode::error ("generation object was not constructed!");
        return InvalidState;
      }

    if (!archiver.constructed ())
      {
        if (config.verbose)
          {
            Genode::log ("archiver is empty! Aborting the snapshot.");
          }

        __abort_snapshot ();
        return InvalidState;
      }

    archiver->commit (*generation);

    __update_references ();

    if (config.verbose)
      Genode::log ("generation committed successfully!");

    __reset_gen ();

    state = Dormant;

    purge_expired ();

    return Ok;
  }

  /*
   * RESTORING SNAPSHOT
   */

  Snapper::Result
  Main::open_generation (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    if (state != Dormant)
      return InvalidState;

    state = Restoration;

    Result res = __remove_unfinished_gen ();
    if (res != Ok)
      return res;

    res = __load_gen (generation);
    if (res == LoadGenFailed)
      {
        state = Dormant;
      }

    return res;
  }

  Snapper::Result
  Main::restore (void *dst, Genode::size_t size,
                 Archive::ArchiveKey identifier)
  {
    if (state != Restoration)
      return InvalidState;

    Snapper::Result res = Ok;
    Genode::memset (dst, 0, size);

    archiver->archive.with_element (
        identifier,
        [&res, &dst, &size] (Archive::ArchiveEntry &entry) {
          entry.queue.for_each (
              [&res, &dst, &size] (Archive::Backlink &backlink) {
                Genode::Byte_range_ptr dst_buf ((char *)dst, size);
                switch (backlink.get_data (dst_buf))
                  {
                  case Archive::Backlink::Error::None:
                    res = Ok;
                    break;
                  case Archive::Backlink::Error::InvalidVersion:
                    res = InvalidVersion;
                    break;
                  case Archive::Backlink::Error::InvalidIntegrity:
                    res = IntegrityFailed;
                    break;
                  case Archive::Backlink::Error::MissingFieldErr:
                    res = IntegrityFailed;
                    break;
                  default:
                    res = RestoreFailed;
                    break;
                  }
              });
        },
        [&res] () { res = NoMatches; });

    return res;
  }

  Snapper::Result
  Main::close_generation (void)
  {
    if (state != Restoration)
      return InvalidState;

    state = Dormant;

    if (generation.constructed ())
      generation.destruct ();

    // INFO Do not destruct the archiver as it will be used as the
    // basis for future snapshots.

    purge_expired ();

    return Ok;
  }

  Snapper::Result
  Main::purge (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    if (state != Dormant)
      {
        return InvalidState;
      }

    /* INFO
       Check that we have the minimum allowed generation count.
    */
    if (__num_gen () - 1 < config.min_snapshots)
      {
        Genode::error ("purging generation will reduce snapshot count to less "
                       "than allowed!");
        return PurgeDenied;
      }

    state = Purge;
    Snapper::Result res = Ok;

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> _gen
        = generation;

    if (_gen == "")
      {
        snapper_root.for_each_entry (
            [this, &_gen] (Genode::Directory::Entry &e) {
              if (_gen == "" || _gen > e.name ())
                {
                  if (__valid_archive (
                          Genode::Directory::join (e.name (), "archive")))
                    {
                      _gen = e.name ();
                    }
                }
            });
      }

    if (_gen == "")
      {
        if (config.verbose)
          Genode::log ("no generation exists for purging");

        goto CLEAN_RET;
      }

    res = __load_gen (_gen);
    if (res == LoadGenFailed)
      goto CLEAN_RET;

    while (true)
      {
        bool has_element = archiver->archive.with_any_element (
            [this] (const Archive::ArchiveEntry &entry) {
              // decrement each backlink's reference count
              entry.queue.for_each ([this] (Archive::Backlink &backlink) {
                bool remove = false;

                backlink.get_reference_count ().with_result (
                    [&backlink, &remove, this] (Snapper::RC reference_count) {
                      reference_count--;

                      // if the reference count is 0 or less, remove the
                      // backlink
                      if (reference_count > 0)
                        {
                          if (backlink.set_reference_count (reference_count)
                                  .failed ())
                            {
                              if (config.integrity)
                                {
                                  Genode::error ("failed to set reference "
                                                 "count of backlink");
                                  throw CrashStates::REF_COUNT_FAILED;
                                }

                              remove = true;
                            }
                        }
                      else
                        remove = true;
                    },
                    [&remove, this] (Archive::Backlink::Error) {
                      if (config.integrity)
                        {
                          Genode::error ("failed to set reference "
                                         "count of backlink");
                          throw CrashStates::REF_COUNT_FAILED;
                        }

                      remove = true;
                    });

                if (remove)
                  __delete_upwards (backlink.value.string ());

                /* INFO
                 * No need to dequeue the Backlink as the entire ArchiveEntry
                 * will be removed.
                 */
              });
              archiver->remove (entry.name);
            });

        if (!has_element)
          {
            break;
          }
      }
    __delete_upwards (Genode::Directory::join (_gen, "archive").string ());
    __reset_gen ();

    if (config.verbose)
      Genode::log ("purged: \"", _gen, "\"");

  CLEAN_RET:
    state = Dormant;
    return res;
  }

  void
  Main::purge_expired (void)
  {
    Genode::uint64_t num_gen = __num_gen ();

    while (config.max_snapshots && num_gen > config.max_snapshots
           && num_gen > config.min_snapshots)
      {
        if (config.verbose)
          Genode::log ("snaphots exceed max number, purging oldest");

        if (purge () != Ok)
          {
            Genode::error ("could not purge oldest generation! It must be "
                           "removed manually!");

            /* INFO
               We must crash here, otherwise non-expired
               generations will be purged!
            */
            throw CrashStates::PURGE_FAILED;
          }

        num_gen = __num_gen ();
      }

    if (!config.expiration)
      return;

    Rtc::Timestamp now = rtc.current_time ();
    Genode::uint64_t expiry = timestamp_to_seconds (now) - config.expiration;

    snapper_root.for_each_entry (
        [this, expiry] (Genode::Directory::Entry &entry) {
          try
            {
              Rtc::Timestamp ts
                  = str_to_timestamp ((char *)entry.name ().string ());

              if (timestamp_to_seconds (ts) < expiry)
                {
                  if (purge (entry.name ()) != Ok)
                    throw -1;

                  if (config.integrity)
                    Genode::log ("purged expired generation: \"",
                                 entry.name (), "\"");
                }
            }
          catch (...)
            {
              Genode::error ("failed to purge expired generation: \"",
                             entry.name (), "\"");
              throw CrashStates::PURGE_FAILED;
            }
        });
  }

  /*
   * HELPER FUNCTIONS
   */

  bool
  Main::__valid_archive (const Genode::Path<Vfs::MAX_PATH_LEN> &archive_path)
  {
    if (!snapper_root.file_exists (archive_path))
      {
        /* INFO
           No log message here, as this branch is commonly used when
           iterating over the <snapper-root>'s entries. Logging here
           would just clutter the logs.
         */

        return false;
      }

    try
      {
        Genode::Readonly_file _archive (snapper_root, archive_path);

        // get data size
        Vfs::file_size data_size
            = snapper_root.file_size (archive_path)
              - sizeof (decltype (Archive::total_backlinks))
              - sizeof (Snapper::CRC) - sizeof (Snapper::VERSION);

        // check version
        Genode::Readonly_file::At pos{ 0 };

        char _version_buf[sizeof (Snapper::VERSION)];
        Genode::Byte_range_ptr version_buf (_version_buf,
                                            sizeof (Snapper::VERSION));

        if (_archive.read (pos, version_buf) == 0)
          {
            // INFO No error message since this could be called from
            // __load_gen() which is not an error, only means that the
            // archive file is not yet valid.
            return false;
          }

        Snapper::VERSION version
            = *(reinterpret_cast<Snapper::VERSION *> (version_buf.start));

        if (version != Version)
          {
            Genode::error ("invalid archive, version mismatch: ", version,
                           ", should be: ", (VERSION)Version);
            return false;
          }

        // check CRC
        pos.value = sizeof (Snapper::VERSION);

        char _crc_buf[sizeof (Snapper::CRC)];
        Genode::Byte_range_ptr crc_buf (_crc_buf, sizeof (Snapper::CRC));

        if (_archive.read (pos, crc_buf) == 0)
          {
            Genode::error ("invalid archive, missing CRC: ", archive_path);
            return false;
          }

        Snapper::CRC crc = *(reinterpret_cast<Snapper::CRC *> (crc_buf.start));

        // get number of entries in the data
        pos.value = sizeof (Snapper::VERSION) + sizeof (Snapper::CRC);

        char _num_backlinks_buf[sizeof (decltype (Archive::total_backlinks))];
        Genode::Byte_range_ptr num_backlinks_buf (_num_backlinks_buf,
                                                  sizeof (_num_backlinks_buf));

        if (_archive.read (pos, num_backlinks_buf) == 0)
          {
            Genode::error (
                "invalid archive, missing the number of data entries: ",
                archive_path);
            return false;
          }

        // get data
        pos.value = sizeof (Snapper::VERSION) + sizeof (Snapper::CRC)
                    + sizeof (decltype (Archive::total_backlinks));

        char *_data_buf = (char *)heap.alloc (data_size);
        Genode::Byte_range_ptr data (_data_buf, data_size);

        if (_archive.read (pos, data) == 0)
          {
            Genode::error ("invalid archive, missing data: ", archive_path);
            heap.free (data.start, data.num_bytes);
            return false;
          }

        // calculate and check crc
        bool integrity = crc != crc32 (data.start, data.num_bytes);
        heap.free (data.start, data.num_bytes);

        if (integrity)
          {
            // INFO No error message since this could be called from
            // __load_gen() which is not an error, only means that the
            // archive file is not yet valid.
            return false;
          }
      }
    catch (Genode::Readonly_file::Open_failed)
      {
        Genode::error ("could not open archive file");
        return false;
      }
    catch (Genode::Out_of_ram)
      {
        Genode::error ("could not allocate heap: out of ram!");
        return false;
      }
    catch (Genode::Out_of_caps)
      {
        Genode::error ("could not allocate heap: out of capabilities!");
        return false;
      }
    catch (Genode::Denied)
      {
        Genode::error ("could not allocate heap: allocation denied!");
        return false;
      }

    return true;
  }

  Snapper::Result
  Main::__remove_unfinished_gen (void)
  {
    Snapper::Result res = Ok;

    snapper_root.for_each_entry ([this, &res] (Genode::Directory::Entry &e) {
      Genode::Path<Vfs::MAX_PATH_LEN> archive
          = Genode::Directory::join (e.name (), "archive");

      if (!__valid_archive (archive))
        {
          snapper_root.unlink (e.name ());
          if (snapper_root.directory_exists (e.name ()))
            {
              Genode::error ("could not remove old generation: ", e.name ());
              res = InitFailed;
            }
        }
    });

    if (config.verbose && res == Ok)
      Genode::log ("no unfinished generation remain.");

    return res;
  }

  Snapper::Result
  Main::__init_gen (void)
  {
    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> timestamp
        = timestamp_to_str (rtc.current_time ());

    // INFO While loop to prevent two identical timestamps (older
    // generation will be overriden by newer one!).
    while (snapper_root.directory_exists (timestamp))
      {
        Genode::warning (
            "generation name is already taken, waiting for a new one...");
        timer.msleep (1000);
        timestamp = timestamp_to_str (rtc.current_time ());
      };

    snapper_root.create_sub_directory (timestamp);
    if (!snapper_root.directory_exists (timestamp))
      {
        Genode::error ("could not create generation directory: ", timestamp);
        return InitFailed;
      }

    generation.construct (snapper_root, timestamp);

    generation->create_sub_directory ("snapshot");
    if (!generation->directory_exists ("snapshot"))
      {
        Genode::error ("could not create snapshot directory: ", timestamp,
                       "/snapshot");

        snapper_root.unlink (timestamp);
        if (snapper_root.directory_exists (timestamp))
          Genode::error ("could not remove directory: ", timestamp,
                         "! It must be manually removed!");

        return InitFailed;
      }

    snapshot.construct (*generation, Genode::Path<11> ("snapshot"));
    snapshot_dir_path = Genode::Directory::join (timestamp, "snapshot");

    return Ok;
  }

  void
  Main::__reset_gen (void)
  {
    snapshot_dir_path = "/";
    snapshot_file_count = 0;

    snapshot.destruct ();
    generation.destruct ();
  }

  Snapper::Result
  Main::__load_gen (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> latest
        = generation;

    bool validity_verified = false;

    if (latest == "")
      {
        snapper_root.for_each_entry ([this, &validity_verified, &latest] (
                                         Genode::Directory::Entry &entry) {
          if (latest == "" || entry.name () > latest)
            {
              latest = entry.name ();
              if (__valid_archive (
                      Genode::Directory::join (entry.name (), "archive")))
                {
                  latest = entry.name ();
                  validity_verified = true;
                }
            }
        });
      }

    if (latest == "")
      return NoPriorGen;

    if (!validity_verified)
      {
        if (!__valid_archive (Genode::Directory::join (latest, "archive")))
          {
            return NoPriorGen;
          }
      }

    try
      {
        if (config.verbose)
          {
            Genode::log ("loading generation: ", latest);
          }

        archiver.construct (*this);

        // INFO Load the latest valid generation.
        Genode::Readonly_file archive_file (
            snapper_root, Genode::Directory::join (latest, "archive"));

        archiver->extract_from_archive_file (archive_file);
      }
    catch (Genode::File::Open_failed)
      {
        Genode::error ("failed to open archive file of generation: ", latest);

        if (config.integrity)
          {
            Genode::error ("invalid archive file failed integrity check");
            throw CrashStates::INVALID_ARCHIVE_FILE;
          }

        return LoadGenFailed;
      }

    return Ok;
  }

  void
  Main::__abort_snapshot (void)
  {
    if (snapshot.constructed ())
      {
        snapshot->unlink ("/");
        snapshot.destruct ();
      }

    snapshot_dir_path = "/";
    snapshot_file_count = 0;

    if (generation.constructed ())
      {
        generation->unlink ("/");
        generation.destruct ();
      }

    archiver.destruct ();
  }

  void
  Main::__update_references (void)
  {
    bool success = false;

    archiver->archive.for_each ([this, &success] (
                                    const Archive::ArchiveEntry &entry) {
      entry.queue.for_each ([this, &success] (Archive::Backlink &backlink) {
        backlink.get_reference_count ().with_result (
            [&backlink, &success] (Snapper::RC rc) {
              if (backlink.set_reference_count (++rc).ok ())
                {
                  success = true;
                }
            },
            [this] (Snapper::Archive::Backlink::Error) {
              if (config.integrity)
                {
                  Genode::error ("invalid snapshot file");
                  throw CrashStates::INVALID_SNAPSHOT_FILE;
                }
            });

        if (success)
          return;
      });

      if (success)
        return;
    });
  }

  Genode::uint64_t
  Main::__num_gen (void)
  {
    Genode::uint64_t num_generations = 0;

    snapper_root.for_each_entry (
        [this, &num_generations] (Genode::Directory::Entry &e) {
          if (__valid_archive (Genode::Directory::join (e, "archive")))
            num_generations++;
        });

    return num_generations;
  }

  Genode::uint64_t
  Main::__num_dirent (const Genode::String<Vfs::MAX_PATH_LEN> &dir)
  {
    Genode::uint64_t count = 0;

    Genode::Directory _dir (snapper_root, dir);
    _dir.for_each_entry ([&count] (Genode::Directory::Entry &) { count++; });

    return count;
  }

  void
  Main::__delete_upwards (const char *location)
  {
    if (Genode::strlen (location) > Vfs::MAX_PATH_LEN)
      {
        Genode::error ("path is too long: \"", Genode::Cstring (location),
                       "\"! Remove manually.");

        throw CrashStates::PURGE_FAILED;
      }

    char _location[Vfs::MAX_PATH_LEN];
    Genode::memset (_location, 0, sizeof (_location));
    Genode::copy_cstring (_location, location, sizeof (_location));

    switch (snapper_root.root_dir ().unlink (location))
      {
      case Vfs::Directory_service::UNLINK_ERR_NO_ENTRY:
        return;

      case Vfs::Directory_service::UNLINK_ERR_NOT_EMPTY:
        Genode::error ("could not delete: \"", Genode::Cstring (location),
                       "\" due to directory not being empty! fix this by "
                       "removing manually!");
        throw CrashStates::PURGE_FAILED;

      case Vfs::Directory_service::UNLINK_ERR_NO_PERM:
        Genode::error (
            "could not delete: \"", Genode::Cstring (location),
            "\" due to lack of permission! fix this by removing manually!");
        throw CrashStates::PURGE_FAILED;

      default:
        if (config.verbose)
          Genode::log ("deleted: \"", location, "\"");
        break;
      }

    remove_basename (_location);
    char *parent_dir = _location;

    if (_location[0])
      {
        if (__num_dirent (Genode::Cstring (parent_dir)) < 1)
          __delete_upwards (parent_dir);
      }
  }

} // namespace Snapper
