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
        config (), generation (static_cast<Vfs::Simple_env &> (snapper_root)),
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
    total_snapshot_objects = 0;

    Result res = __remove_unfinished_gen ();
    if (res != Ok)
      return res;

    res = __init_gen ();
    if (res != Ok)
      return res;

    res = __load_gen ();
    if (res == LoadGenFailed)
      {
        state = Dormant;
      }

    return res;
  }

  Snapper::Result
  Main::take_snapshot (void const *const payload, Genode::uint64_t size,
                       Archive::ArchiveKey identifier)
  {
    if (state != Creation)
      return InvalidState;

    bool new_file_needed = false;
    Snapper::CRC crc = crc32 (payload, size);

    // check if identifier exists in the mapping and if the crc
    // matches the calculated crc of the payload.
    archiver->archive.with_element (
        identifier,
        [this, &new_file_needed, &crc] (Archive::ArchiveEntry &entry) {
          /* INFO
             Only need to check the latest backlink as it is assumed
             that all backlinks in the queue store the same data as
             part of the redundant policy.
           */
          entry.queue.head ([this, &entry, &new_file_needed,
                             &crc] (Archive::Backlink &backlink) {
            backlink.get_version ().with_result (
                [this, backlink, &new_file_needed] (Snapper::VERSION version) {
                  if (version != Version)
                    {
                      if (config.verbose)
                        Genode::log ("backlink has a version mismatch: ",
                                     backlink.value,
                                     ". Creating a new snapshot file.");

                      new_file_needed = true;
                    }
                },
                [&new_file_needed] (Snapper::Archive::Backlink::Error) {
                  new_file_needed = true;
                });

            if (new_file_needed)
              {
                while (!entry.queue.empty ())
                  {
                    entry.queue.dequeue ([this] (Archive::Backlink &backlink) {
                      Genode::destroy (heap, backlink._self);
                    });
                  }

                return;
              }

            backlink.get_integrity ().with_result (
                [this, backlink, crc,
                 &new_file_needed] (Snapper::CRC file_crc) {
                  if (file_crc != crc)
                    {
                      if (config.verbose)
                        Genode::log (
                            "backlink has a mismatching crc: ", backlink.value,
                            ". Creating new snapshot file.");

                      new_file_needed = true;
                    }
                },
                [&new_file_needed] (Snapper::Archive::Backlink::Error) {
                  new_file_needed = true;
                });

            if (new_file_needed)
              {
                while (!entry.queue.empty ())
                  {
                    entry.queue.dequeue ([this] (Archive::Backlink &backlink) {
                      Genode::destroy (heap, backlink._self);
                    });
                  }

                return;
              }

            backlink.get_reference_count ().with_result (
                [this, &backlink, &new_file_needed] (Snapper::RC rc) {
                  if (rc >= config.redundancy)
                    {
                      if (config.verbose)
                        Genode::log ("backlink reference count exceeded: ",
                                     backlink.value,
                                     ". Creating redundant copy.");

                      new_file_needed = true;
                    }
                  else
                    {
                      if (backlink.set_reference_count (rc + 1).failed ())
                        {
                          Genode::error (
                              "could not update reference count of: ",
                              backlink.value, "! Creating new file instead.");

                          new_file_needed = true;
                        }
                    }
                },
                [this, &entry,
                 &new_file_needed] (Snapper::Archive::Backlink::Error) {
                  new_file_needed = true;

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
        [&new_file_needed] () { new_file_needed = true; });

    /* INFO
       Increment total_snapshot_objects before returning, because we
       will need this number when allocating space for the archive CRC
       in commit_snapshot().
     */
    total_snapshot_objects++;

    if (!new_file_needed)
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

        // writing Snapper version
        Genode::New_file::Append_result res
            = file.append ((char *)&ver, sizeof (Snapper::VERSION));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("could not write version to file: ", filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        // writing crc
        res = file.append ((char *)&crc, sizeof (Snapper::CRC));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("could not write CRC to file: ", filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        // writing reference count
        /* INFO
         The reference count will be incremented when the full
         snapshot is committed. It starts at 0, because the snapshot
         file is currently not referenced by any archive file.
        */
        Snapper::RC reference_count = 0;
        res = file.append ((char *)&reference_count, sizeof (Snapper::RC));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("could not write reference count to file: ",
                           filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        // writing payload
        res = file.append ((char *)payload, size);

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("could not write payload to file: ", filepath_base);
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

    try
      {
        Genode::New_file archive (*generation, "archive");

        if (!archiver.constructed ())
          {
            if (config.verbose)
              {
                Genode::log ("archiver is empty! Aborting the snapshot.");
              }

            __abort_snapshot ();
            return InvalidState;
          }

        constexpr Genode::size_t key_size = sizeof (Archive::ArchiveKey);
        constexpr Genode::size_t val_size
            = sizeof (decltype (Archive::Backlink::value));
        constexpr Genode::size_t size = key_size + val_size;

        /* INFO
         * Save archive entries into a buffer first, so that the CRC
         * can be calculated. Then write the buffer to the archive
         * file.
         */
        char *_archive_buf
            = (char *)heap.alloc (size * total_snapshot_objects);

        Genode::memset (_archive_buf, 0, size * total_snapshot_objects);

        Genode::uint64_t idx = 0;

        archiver->archive.for_each (
            [&_archive_buf, &idx] (const Archive::ArchiveEntry &entry) {
              entry.queue.for_each ([_archive_buf, &idx, &entry] (
                                        const Archive::Backlink &backlink) {
                Genode::memcpy (_archive_buf + (idx * size), &entry.name,
                                key_size);

                Genode::memcpy (_archive_buf + (idx * size + key_size),
                                backlink.value.string (), val_size);

                idx++;
              });
            });

        Snapper::VERSION ver = Version;
        Snapper::CRC crc = crc32 (_archive_buf, size * total_snapshot_objects);

        Genode::New_file::Append_result res
            = archive.append ((char *)&ver, sizeof (Snapper::VERSION));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("failed to write the version to the archive file! "
                           "Aborting the snapshot!");

            __abort_snapshot ();

            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        res = archive.append ((char *)&crc, sizeof (Snapper::CRC));
        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("failed to write the CRC to the archive file! "
                           "Aborting the snapshot!");

            __abort_snapshot ();

            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        res = archive.append (_archive_buf, size * total_snapshot_objects);

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error (
                "failed to write the archive entry to the archive file! "
                "Aborting the snapshot!");

            __abort_snapshot ();

            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        heap.free (_archive_buf, size * total_snapshot_objects);
      }
    catch (Genode::New_file::Create_failed)
      {
        Genode::error ("failed to create archive file for this generation! "
                       "Aborting the snapshot!");

        __abort_snapshot ();

        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Out_of_ram)
      {
        Genode::error ("snapper is out of RAM! Aborting the snapshot!");
        __abort_snapshot ();

        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Out_of_caps)
      {
        Genode::error (
            "snapper is out of capabilities! Aborting the snapshot!");
        __abort_snapshot ();

        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Denied)
      {
        Genode::error ("memory allocation denied! Aborting the snapshot!");
        __abort_snapshot ();

        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }

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
        [this, &res, &dst, &size] (Archive::ArchiveEntry &entry) {
          entry.queue.for_each ([this, &res, &dst,
                                 &size] (Archive::Backlink &backlink) {
            backlink.get_version ().with_result (
                [this, backlink, &res] (Snapper::VERSION version) {
                  if (version != Version)
                    {
                      if (config.verbose)
                        Genode::warning ("backlink has a wrong version: ",
                                         backlink.value);
                      res = InvalidVersion;
                    }
                  else
                    res = Ok;
                },
                [this, backlink, &res] (Snapper::Archive::Backlink::Error) {
                  if (config.verbose)
                    Genode::warning ("could not access backlink's version: ",
                                     backlink.value);
                  res = InvalidVersion;
                });

            if (res != Ok)
              return;

            Snapper::CRC crc = 0;
            backlink.get_integrity ().with_result (
                [&crc] (Snapper::CRC _crc) { crc = _crc; },
                [this, backlink, &res] (Snapper::Archive::Backlink::Error) {
                  if (config.verbose)
                    {
                      Genode::warning ("could not access backlink's crc: ",
                                       backlink.value);
                    }
                  res = IntegrityFailed;
                });

            if (res != Ok)
              return;

            Genode::Byte_range_ptr buf ((char *)dst, size);

            if (backlink.get_data (buf) != Snapper::Archive::Backlink::None)
              {
                res = RestoreFailed;
              }

            if (crc32 (buf.start, buf.num_bytes) != crc)
              {
                if (config.verbose)
                  Genode::warning (
                      "backlink has an invalid CRC: ", backlink.value,
                      "! Remove it to "
                      "not receive this warning again.");

                res = IntegrityFailed;
                Genode::memset (buf.start, 0, buf.num_bytes);
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

    if (archiver.constructed ())
      archiver.destruct ();

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
        snapper_root.for_each_entry ([this, &_gen] (Genode::Directory::Entry &e) {
          if (_gen == "" || _gen > e.name ())
            {
              if (__valid_archive (
                    Genode::Directory::join (e.name (), "archive"))) {
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
  Main::__valid_archive (const Genode::Path<Vfs::MAX_PATH_LEN> &archive)
  {
    if (!snapper_root.file_exists (archive))
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
        Genode::Readonly_file _archive (snapper_root, archive);

        // check version
        Genode::Readonly_file::At pos{ 0 };

        char _version_buf[sizeof (Snapper::VERSION)];
        Genode::Byte_range_ptr version_buf (_version_buf,
                                            sizeof (Snapper::VERSION));

        if (_archive.read (pos, version_buf) == 0)
          {
            Genode::error ("invalid archive, missing version: ", archive);
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
        pos.value += sizeof (Snapper::VERSION);

        char _crc_buf[sizeof (Snapper::CRC)];
        Genode::Byte_range_ptr crc_buf (_crc_buf, sizeof (Snapper::CRC));

        if (_archive.read (pos, crc_buf) == 0)
          {
            Genode::error ("invalid archive, missing CRC: ", archive);
            return false;
          }

        Snapper::CRC crc = *(reinterpret_cast<Snapper::CRC *> (crc_buf.start));

        // get data size
        Vfs::Dir_file_system::Stat stats;
        if (snapper_root.root_dir ().stat (archive.string (), stats)
            != Vfs::Dir_file_system::STAT_OK)
          {
            Genode::error ("could not open the stats for: ", archive);
            return false;
          }

        Genode::size_t size
            = stats.size - sizeof (Snapper::VERSION) - sizeof (Snapper::CRC);

        // get data
        pos.value += sizeof (Snapper::CRC);

        char *_data_buf = (char *)heap.alloc (size);
        Genode::Byte_range_ptr data (_data_buf, size);

        if (_archive.read (pos, data) == 0)
          {
            Genode::error ("invalid archive, missing data: ", archive);
            return false;
          }

        // calculate and check crc
        bool integrity = crc != crc32 (data.start, data.num_bytes);
        heap.free (data.start, data.num_bytes);

        if (integrity)
          {
            Genode::error ("invalid archive, integrity check failed: ",
                           archive);
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
    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> latest = "";

    snapper_root.for_each_entry ([&latest] (Genode::Directory::Entry &e) {
      if (latest == "" || e.name () > latest)
        {
          latest = e.name ();
        }
    });

    if (latest != "")
      {
        Genode::Path<Vfs::MAX_PATH_LEN> old
            = Genode::Directory::join (latest, "archive");

        if (!snapper_root.file_exists (old))
          {
            snapper_root.unlink (latest);
            if (snapper_root.directory_exists (latest))
              {
                Genode::error ("could not remove old generation: ", latest);
                return InitFailed;
              }
          }
      }

    if (config.verbose)
      Genode::log ("no unfinished generation remain.");

    return Ok;
  }

  Snapper::Result
  Main::__init_gen (void)
  {
    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> timestamp
        = timestamp_to_str (rtc.current_time ());

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
    total_snapshot_objects = 0;

    snapshot.destruct();
    generation.destruct ();

    archiver.destruct ();
  }

  Snapper::Result
  Main::__load_gen (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    archiver.construct (*this);

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> latest
        = generation;

    if (latest == "")
      {
        snapper_root.for_each_entry (
            [this, &latest] (Genode::Directory::Entry &entry) {
              if (latest == "" || entry.name () > latest)
                {
                  if (__valid_archive (
                          Genode::Directory::join (entry.name (), "archive")))
                    {
                      latest = entry.name ();
                    }
                }
            });
      }

    if (latest == "")
      return NoPriorGen;

    try
      {
        if (config.verbose) {
          Genode::log("loading generation: ", latest);
        }

        Genode::Path<Vfs::MAX_PATH_LEN> archive_path
            = Genode::Directory::join (latest, "archive");

        if (!__valid_archive (archive_path))
          {
            if (config.integrity)
              {
                Genode::error ("invalid archive file failed integrity check");
                throw CrashStates::INVALID_ARCHIVE_FILE;
              }

            return LoadGenFailed;
          }

        Genode::Readonly_file archive_file (snapper_root, archive_path);

        Genode::Readonly_file::At pos (sizeof (Snapper::VERSION)
                                       + sizeof (Snapper::CRC));

        char _key_buf[sizeof (Archive::ArchiveKey)];
        char _val_buf[sizeof (decltype (Archive::Backlink::value))];

        Genode::Byte_range_ptr key_buf (_key_buf, sizeof (_key_buf));
        Genode::Byte_range_ptr val_buf (_val_buf, sizeof (_val_buf));

        while (true)
          {
            Genode::size_t bytes_read = archive_file.read (pos, key_buf);
            if (bytes_read == 0)
              {
                break;
              }

            pos.value += bytes_read;

            bytes_read = archive_file.read (pos, val_buf);
            if (bytes_read == 0)
              break;

            pos.value += bytes_read;

            Archive::ArchiveKey key
                = *(reinterpret_cast<Archive::ArchiveKey *> (key_buf.start));

            archiver->insert (key, Genode::Cstring (val_buf.start));
          }
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
    total_snapshot_objects = 0;

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
