#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"

[[maybe_unused]] static Genode::String<
    Vfs::Directory_service::Dirent::Name::MAX_LEN>
timestamp_to_str (const Rtc::Timestamp &ts)
{
  Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> str (
      ts.year, "-", ts.month, "-", ts.day, " ", ts.hour, ":", ts.minute, ":",
      ts.second);

  return str;
}

// TODO better error handling
[[maybe_unused]] static Rtc::Timestamp
str_to_timestamp (char *str)
{
  if (Genode::strlen (str) > Vfs::Directory_service::Dirent::Name::MAX_LEN)
    {
      throw -1;
    }

  char *ptr = str;
  const unsigned base = 10;

  Rtc::Timestamp ts;

  // year
  Genode::size_t size
      = Genode::ascii_to_unsigned<decltype (ts.year)> (ptr, ts.year, base);

  if (size < 4)
    throw -1;

  ptr += size + 1;

  // month
  size = Genode::ascii_to_unsigned<decltype (ts.month)> (ptr, ts.month, base);
  if (size != 1 && size != 2)
    throw -1;

  ptr += size + 1;

  // day
  size = Genode::ascii_to_unsigned<decltype (ts.day)> (ptr, ts.day, base);
  if (size != 1 && size != 2)
    throw -1;

  ptr += size + 1;

  // hour
  size = Genode::ascii_to_unsigned<decltype (ts.hour)> (ptr, ts.hour, base);
  if (size != 1 && size != 2)
    throw -1;

  ptr += size + 1;

  // minute
  size
      = Genode::ascii_to_unsigned<decltype (ts.minute)> (ptr, ts.minute, base);
  if (size != 1 && size != 2)
    throw -1;

  ptr += size + 1;

  // second
  size
      = Genode::ascii_to_unsigned<decltype (ts.second)> (ptr, ts.second, base);
  if (size != 1 && size != 2)
    throw -1;

  return ts;
}

[[maybe_unused]] static bool
leap_year (unsigned year)
{
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

[[maybe_unused]] static unsigned
days_in_month (unsigned month, unsigned year)
{
  static const unsigned days[]
      = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  if (month == 2 && leap_year (year))
    return 29;

  return days[month - 1];
}

[[maybe_unused]] Genode::uint64_t
timestamp_to_seconds (const Rtc::Timestamp &ts)
{
  Genode::uint64_t seconds = 0;

  // add seconds for complete years since epoch (1970)
  for (unsigned y = 1970; y < ts.year; y++)
    {
      seconds += leap_year (y) ? 366ULL * 24 * 60 * 60 : 365ULL * 24 * 60 * 60;
    }

  // add seconds for complete months in current year
  for (unsigned m = 1; m < ts.month; m++)
    {
      seconds += days_in_month (m, ts.year) * 24ULL * 60 * 60;
    }

  // Add remaining days, hours, minutes and seconds
  seconds += (ts.day - 1) * 24ULL * 60 * 60;
  seconds += ts.hour * 60ULL * 60;
  seconds += ts.minute * 60ULL;
  seconds += ts.second;

  return seconds;
}

namespace SnapperNS
{
  /*
   * STATIC VARIABLES
   */
  Snapper *snapper = nullptr;
  Snapper *Snapper::instance = nullptr;

  const Genode::uint8_t Snapper::Version = 2;

  /*
   * CONSTRUCTORS
   */

  /* TODO
   * snapper_root should be initialized based off of a confurable
   * path. That will also then affect the initialization of
   * snapshot_dir_path.
   */
  Snapper::Snapper (Genode::Env &env)
      : snapper_config (), env (env), config (env, "config"),
        heap (env.ram (), env.rm ()),
        snapper_root (env, heap, config.xml ().sub_node ("vfs")), rtc (env),
        generation (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot_dir_path ("/"), archiver ()
  {
    snapper_config.verbose
        = config.xml ().attribute_value<decltype (Snapper::Config::verbose)> (
            "verbose", Snapper::Config::_verbose);

    snapper_config.redundancy
        = config.xml ()
              .attribute_value<decltype (Snapper::Config::redundancy)> (
                  "redundancy", Snapper::Config::_redundancy);

    snapper_config.integrity
        = config.xml ()
              .attribute_value<decltype (Snapper::Config::integrity)> (
                  "integrity", Snapper::Config::_integrity);

    snapper_config.threshold
        = config.xml ()
              .attribute_value<decltype (Snapper::Config::threshold)> (
                  "threshold", Snapper::Config::_threshold);

    snapper_config.max_snapshots
        = config.xml ()
              .attribute_value<decltype (Snapper::Config::max_snapshots)> (
                  "max_snapshots", Snapper::Config::_max_snapshots);

    snapper_config.min_snapshots
        = config.xml ()
              .attribute_value<decltype (Snapper::Config::min_snapshots)> (
                  "min_snapshots", Snapper::Config::_min_snapshots);

    snapper_config.expiration
        = config.xml ()
              .attribute_value<decltype (Snapper::Config::expiration)> (
                  "expiration", Snapper::Config::_expiration);
  }

  Snapper::~Snapper ()
  {
    generation.destruct ();
    snapshot.destruct ();
    archiver.destruct ();

    instance = nullptr;

    if (snapper_config.verbose)
      Genode::log ("snapper object destroyed.");
  }

  // TODO: rewrite this to use Reconstructible.
  Snapper *
  Snapper::new_snapper (Genode::Env &env)
  {
    if (Snapper::instance)
      return Snapper::instance;

    static Snapper local_snapper (env);
    env.exec_static_constructors ();

    Snapper::instance = &local_snapper;

    if (Snapper::instance->snapper_config.verbose)
      Genode::log ("new snapper object created.");

    return Snapper::instance;
  }

  /*
   * CREATING SNAPSHOT
   */
  Snapper::Result
  Snapper::init_snapshot ()
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
  Snapper::take_snapshot (void const *const payload, Genode::uint64_t size,
                          Archive::ArchiveKey identifier)
  {
    if (state != Creation)
      return InvalidState;

    bool new_file_needed = true;
    Snapper::CRC crc = 0;
    Snapper::RC reference_count = 0;

    TODO ("calculate crc of payload");

    // check if identifier exists in the mapping and if the crc
    // matches the calculated crc of the payload.
    archiver->archive.with_element (
        identifier,
        [this, &new_file_needed, &crc,
         &reference_count] (Archive::ArchiveEntry &entry) {
          while (!entry.queue.empty () || !new_file_needed)
            {
              entry.queue.head ([this, &entry, &new_file_needed, &crc,
                                 &reference_count] (
                                    Archive::Backlink &backlink) {
                try
                  {
                    Snapper::VERSION version = 0;

                    Genode::Readonly_file file (snapper_root,
                                                backlink.value.string ());

                    Genode::Readonly_file::At version_pos{ 0 };
                    constexpr Genode::size_t version_size
                        = sizeof (Snapper::VERSION);

                    char _ver_buf[version_size];
                    Genode::Byte_range_ptr ver_buf (_ver_buf, version_size);

                    if (file.read (version_pos, ver_buf) == 0)
                      {
                        entry.queue.dequeue ([] (Archive::Backlink &backlink) {
                          Genode::error (
                              "snapshot file has no version: ", backlink.value,
                              ". Removing it from future backlink queue.");

                          Genode::destroy (snapper->heap, backlink._self);
                        });
                      }

                    version = *(
                        reinterpret_cast<Snapper::VERSION *> (ver_buf.start));

                    if (version != Snapper::Version)
                      {
                        entry.queue.dequeue ([] (Archive::Backlink &backlink) {
                          Genode::error (
                              "snapshot file has an invalid version: ",
                              backlink.value,
                              ". Removing it from future backlink queue.");

                          Genode::destroy (snapper->heap, backlink._self);
                        });
                      }

                    Genode::Readonly_file::At crc_pos{ version_pos.value
                                                       + version_size };
                    constexpr Genode::size_t crc_size = sizeof (Snapper::CRC);
                    char _crc_buf[crc_size];
                    Genode::Byte_range_ptr crc_buf (_crc_buf, crc_size);

                    if (file.read (crc_pos, crc_buf) == 0)
                      {
                        entry.queue.dequeue ([] (Archive::Backlink &backlink) {
                          Genode::error (
                              "snapshot file has no CRC value: ",
                              backlink.value,
                              ". Removing it from future backlink queue.");

                          Genode::destroy (snapper->heap, backlink._self);
                        });
                      }

                    crc = *(reinterpret_cast<Snapper::CRC *> (crc_buf.start));
                    TODO ("check crc");

                    Genode::Readonly_file::At rc_pos{ crc_pos.value
                                                      + crc_size };

                    constexpr Genode::size_t rc_size = sizeof (Snapper::RC);

                    char _rc_buf[rc_size];
                    Genode::Byte_range_ptr rc_buf (_rc_buf, rc_size);
                    if (file.read (rc_pos, rc_buf) == 0)
                      {
                        entry.queue.dequeue ([] (Archive::Backlink &backlink) {
                          Genode::error (
                              "snapshot file has no reference count: ",
                              backlink.value,
                              ". Removing it from future backlink queue.");

                          Genode::destroy (snapper->heap, backlink._self);
                        });
                      }

                    reference_count
                        = *(reinterpret_cast<Snapper::RC *> (rc_buf.start));

                    if (reference_count >= snapper_config.redundancy)
                      {
                        if (snapper_config.verbose)
                          {
                            Genode::log (
                                "reference count exceeds redundancy level. "
                                "Creating a redundant backlink.");
                          }

                        new_file_needed = true;
                      }
                    else
                      {
                        new_file_needed = false;
                      }
                  }
                catch (Genode::File::Open_failed)
                  {
                    entry.queue.dequeue ([] (Archive::Backlink &backlink) {
                      Genode::error (
                          "failed to open snapshot file: ", backlink.value,
                          ". Removing it from future backlink queue.");

                      Genode::destroy (snapper->heap, backlink._self);
                    });
                  }
              });
            }
        },
        [&new_file_needed] () { new_file_needed = true; });

    if (!new_file_needed)
      return Ok;

    // create a new snapshot file and write to it the payload metadata
    // and the payload data

    total_snapshot_objects++;
    snapshot_file_count++;

    if (snapshot_file_count >= snapper_config.threshold)
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
        Genode::New_file file (*snapshot, filepath_base);

        // writing Snapper version
        Genode::New_file::Append_result res
            = file.append ((char *)&Version, sizeof (Snapper::VERSION));

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
  Snapper::commit_snapshot (void)
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
            if (snapper_config.verbose)
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

        Snapper::CRC crc = crc32(_archive_buf, size * total_snapshot_objects);

        Genode::New_file::Append_result res
            = archive.append ((char *)&Version, sizeof (Snapper::VERSION));

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

    if (snapper_config.verbose)
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
  Snapper::open_generation (
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
  Snapper::restore (void *dst, Genode::size_t size,
                    Archive::ArchiveKey identifier)
  {
    if (state != Restoration)
      return InvalidState;

    Snapper::Result res = Ok;

    archiver->archive.with_element (
        identifier,
        [&res, &dst, &size] (Archive::ArchiveEntry &entry) {
          entry.queue.for_each ([&res, &dst,
                                 &size] (Archive::Backlink &backlink) {
            backlink.get_version ().with_result (
                [&res] (Snapper::VERSION version) {
                  if (version != Version)
                    res = InvalidVersion;
                  else
                    res = Ok;
                },
                [&res] (Snapper::Archive::Backlink::Error) {
                  res = InvalidVersion;
                });

            if (res != Ok)
              return;

            TODO ("check crc");

            Genode::Byte_range_ptr buf ((char *)dst, size);

            if (backlink.get_data (buf) != Snapper::Archive::Backlink::None)
              {
                res = RestoreFailed;
              }
          });
        },
        [&res] () { res = NoMatches; });

    return res;
  }

  Snapper::Result
  Snapper::close_generation (void)
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
  Snapper::purge (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    if (state != Dormant)
      {
        return InvalidState;
      }

    if (__num_gen () - 1 < snapper_config.min_snapshots)
      {
        Genode::error ("purging generation will reduce snapshot count to less "
                       "than allowed!");
        return PurgeDenied;
      }

    /* INFO
       Check that we have the minimum allowed generation count.
    */

    state = Purge;
    Snapper::Result res = Ok;

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> _gen
        = generation;

    if (_gen == "")
      {
        snapper_root.for_each_entry ([&_gen] (Genode::Directory::Entry &e) {
          if (_gen == "" || _gen > e.name ())
            {
              _gen = e.name ();
            }
        });
      }

    if (_gen == "")
      {
        if (snapper->snapper_config.verbose)
          Genode::log ("no generation exists for purging");

        goto CLEAN_RET;
      }

    res = __load_gen (_gen);
    if (res != Ok)
      goto CLEAN_RET;

    archiver->archive.for_each ([this] (const Archive::ArchiveEntry &entry) {
      // decrement each backlink's reference count
      entry.queue.for_each ([this] (Archive::Backlink &backlink) {
        bool remove = false;

        backlink.get_reference_count ().with_result (
            [&backlink, &remove] (Snapper::RC reference_count) {
              reference_count--;

              // if the reference count is 0 or less, remove the
              // backlink
              if (reference_count > 0)
                {
                  if (backlink.set_reference_count (reference_count).failed ())
                    {
                      if (snapper->snapper_config.integrity)
                        throw CrashStates::REF_COUNT_FAILED;

                      remove = true;
                    }
                }
              else
                remove = true;
            },
            [&remove] (Archive::Backlink::Error) {
              if (snapper->snapper_config.integrity)
                throw CrashStates::REF_COUNT_FAILED;

              remove = true;
            });

        if (remove)
          {
            snapper_root.unlink (backlink.value);
            if (snapper_root.file_exists (backlink.value))
              {
                Genode::error (
                    "could not remove file (requires manual deletion): ",
                    backlink.value);
                throw CrashStates::PURGE_FAILED;
              }
          }

        /* INFO
         * No need to dequeue the Backlink as the entire ArchiveEntry
         * will be removed.
         */
      });
      archiver->remove (entry.name);
    });

    TODO ("remove empty directories");

    __reset_gen ();

    if (snapper_config.verbose)
      Genode::log ("purged: \"", _gen, "\"");

  CLEAN_RET:
    state = Dormant;
    return res;
  }

  void
  Snapper::purge_expired (void)
  {
    Genode::uint64_t num_gen = __num_gen ();

    while (snapper_config.max_snapshots
           && num_gen > snapper_config.max_snapshots
           && num_gen > snapper_config.min_snapshots)
      {
        if (snapper_config.verbose)
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

    if (!snapper_config.expiration)
      return;

    Rtc::Timestamp now = snapper->rtc.current_time ();
    Genode::uint64_t expiry
        = timestamp_to_seconds (now) - snapper_config.expiration;

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

                  if (snapper_config.integrity)
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
  Snapper::__valid_archive (const Genode::Path<Vfs::MAX_PATH_LEN> &archive)
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

        if (version != snapper->Version)
          {
            Genode::error ("invalid archive, version mismatch: ", version,
                           ", should be: ", snapper->Version);
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
        if (snapper->snapper_root.root_dir ().stat (archive.string (), stats)
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
        heap.free(data.start, data.num_bytes);
        
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
  Snapper::__remove_unfinished_gen (void)
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

    if (snapper_config.verbose)
      Genode::log ("no unfinished generation remain.");

    return Ok;
  }

  Snapper::Result
  Snapper::__init_gen (void)
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
  Snapper::__reset_gen (void)
  {
    snapshot_dir_path = "/";
    snapshot_file_count = 0;
    total_snapshot_objects = 0;

    generation.destruct ();

    archiver.destruct ();
  }

  Snapper::Result
  Snapper::__load_gen (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    archiver.construct ();

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
      return Ok;

    try
      {
        Genode::Path<Vfs::MAX_PATH_LEN> archive_path
            = Genode::Directory::join (latest, "archive");

        if (!__valid_archive (archive_path))
          {
            if (snapper_config.integrity)
              {
                throw CrashStates::INVALID_ARCHIVE_FILE;
              }

            return LoadGenFailed;
          }

        Genode::Readonly_file archive_file (snapper_root, archive_path);

        Genode::Readonly_file::At pos (sizeof (Snapper::VERSION)
                                       + sizeof (Snapper::CRC));

        char _key_buf[sizeof (Archive::ArchiveKey)];
        char _val_buf[Vfs::MAX_PATH_LEN];

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

            archiver->insert (key, (char const *)val_buf.start);
          }
      }
    catch (Genode::File::Open_failed)
      {
        Genode::error ("failed to open archive file of generation: ", latest);

        if (snapper_config.integrity)
          {
            throw CrashStates::INVALID_ARCHIVE_FILE;
          }

        return LoadGenFailed;
      }

    return Ok;
  }

  void
  Snapper::__abort_snapshot (void)
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
  Snapper::__update_references (void)
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
              if (snapper_config.integrity)
                {
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
  Snapper::__num_gen (void)
  {
    Genode::uint64_t num_generations = 0;

    snapper_root.for_each_entry ([this, &num_generations] (
                                     Genode::Directory::Entry &e) {
      if (__valid_archive (Genode::Directory::join (e, "archive")))
        num_generations++;
    });

    return num_generations;
  }
} // namespace SnapperNS
