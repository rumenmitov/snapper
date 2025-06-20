#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"

/*
 * UTILITIES
 */
[[maybe_unused]] static Genode::String<
    Vfs::Directory_service::Dirent::Name::MAX_LEN>
timestamp_to_str (const Rtc::Timestamp &ts)
{
  Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> str (
      ts.year, "-", ts.month, "-", ts.day, " ", ts.hour, ":", ts.minute, ":",
      ts.second, ":", ts.microsecond);

  return str;
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
  Snapper::Snapper (Genode::Env &env, const Snapper::Config &snapper_config)
      : env (env), config (env, "config"), heap (env.ram (), env.rm ()),
        snapper_root (env, heap, config.xml ().sub_node ("vfs")), rtc (env),
        generation (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot_dir_path ("/"), snapper_config (snapper_config), archiver ()
  {
  }

  Snapper::~Snapper ()
  {
    generation.destruct ();
    snapshot.destruct ();
    archiver.destruct ();

    instance = nullptr;

    if (snapper_config.verbose)
      Genode::log ("Snapper object destroyed.");
  }

  Snapper *
  Snapper::new_snapper (Genode::Env &env, const Snapper::Config &config)
  {
    if (Snapper::instance)
      return Snapper::instance;

    static Snapper local_snapper (env, config);
    env.exec_static_constructors ();

    Snapper::instance = &local_snapper;

    if (config.verbose)
      Genode::log ("New snapper object created.");

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

    Result res = __remove_unfinished_gen ();
    if (res != Ok)
      return res;

    res = __init_gen ();
    if (res != Ok)
      return res;

    res = __load_gen ();

    return res;
  }

  Snapper::Result
  Snapper::take_snapshot (void const *const payload, Genode::uint64_t size,
                          Archive::ArchiveKey identifier)
  {
    if (state != Creation)
      return InvalidState;

    bool new_file_needed = true;
    Genode::uint8_t reference_count = 0;
    Genode::uint32_t crc = 0;

    TODO ("calculate crc of payload");

    // check if identifier exists in the mapping and if the crc
    // matches the calculated crc of the payload.
    archiver->archive.with_element (
        identifier,
        [this, &new_file_needed] (Archive::ArchiveEntry &entry) {
          while (!entry.queue.empty () || !new_file_needed)
            {
              entry.queue.head ([this, &entry, &new_file_needed] (
                                    Archive::Backlink &backlink) {
                try
                  {
                    Genode::Readonly_file file (snapper_root, backlink.value.string());

                    TODO ("read crc and rc of file and decide whether it "
                          "needs to be updated");

                    new_file_needed = false;
                  }
                catch (Genode::File::Open_failed)
                  {

                    entry.queue.dequeue ([] (Archive::Backlink &backlink) {
                      Genode::error (
                          "Failed to open snapshot file: ", backlink.value,
                          ". Removing it from future backlink queue.");
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

    if (snapshot_file_count >= SNAPPER_THRESH)
      {
        snapshot->create_sub_directory ("ext");
        if (!snapshot->directory_exists ("ext"))
          {
            Genode::error ("Could not create extender sub-directory!");
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
            = file.append ((char *)&Version, sizeof (decltype (Version)));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Could not write version to file: ", filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        // writing reference count
        res = file.append ((char *)&reference_count,
                           sizeof (decltype (reference_count)));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Could not write reference count to file: ",
                           filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        // writing crc
        res = file.append ((char *)&crc, sizeof (decltype (crc)));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Could not write CRC to file: ", filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        // writing payload
        res = file.append ((char *)&payload, size);

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Could not write payload to file: ", filepath_base);
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }
      }
    catch (Genode::New_file::Create_failed)
      {
        Genode::error ("Could not create file: ", filepath_base);
        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }

    // save the snapshot file's path (i.e. a backlink) into the archive
    // (relative to snapper_root)

    Archive::Backlink backlink (
        Genode::Directory::join (snapshot_dir_path, filepath_base));

    archiver->archive.with_element (
        identifier,
        [&backlink] (Archive::ArchiveEntry &entry) {
          entry.queue.enqueue (backlink);
        },
        [this, identifier, &backlink] () {
          Genode::Fifo<Archive::Backlink> queue;
          queue.enqueue (backlink);

          Archive::ArchiveEntry _ (identifier, queue, archiver->archive);
        });

    return Ok;
  }

  Snapper::Result
  Snapper::commit_snapshot (void)
  {
    if (state != Creation)
      return InvalidState;

    if (!generation.constructed ())
      {
        Genode::error ("Generation object was not constructed!");
        return InvalidState;
      }

    try
      {
        Genode::New_file archive (*generation, "archive");

        if (!archiver.constructed ())
          {
            if (snapper_config.verbose)
              {
                Genode::log ("Archiver is empty! Aborting the snapshot.");
              }

            __abort_snapshot ();
          }

        constexpr Genode::size_t key_size = sizeof (Archive::ArchiveKey);
        constexpr Genode::size_t val_size = sizeof (Archive::Backlink);
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
              entry.queue.for_each (
                  [&_archive_buf, &idx,
                   &entry] (Archive::Backlink &backlink) {
                    Genode::memcpy (_archive_buf + (idx * size), &entry.name,
                                    key_size);

                    Genode::memcpy (_archive_buf + (idx * size + key_size),
                                    backlink.value.string (), val_size);
                  });
            });

        Genode::uint64_t crc;
        TODO ("calculate CRC of archive");

        Genode::New_file::Append_result res
            = archive.append ((char *)&Version, sizeof (decltype (Version)));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Failed to write the version to the archive file! "
                           "Aborting the snapshot!");

            __abort_snapshot ();

            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        res = archive.append ((char *)&crc, sizeof (decltype (crc)));
        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Failed to write the CRC to the archive file! "
                           "Aborting the snapshot!");

            __abort_snapshot ();

            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }

        res = archive.append (_archive_buf, size * total_snapshot_objects);

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error (
                "Failed to write the archive entry to the archive file! "
                "Aborting the snapshot!");

            __abort_snapshot ();

            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }
      }
    catch (Genode::New_file::Create_failed)
      {
        Genode::error ("Failed to create archive file for this generation! "
                       "Aborting the snapshot!");

        __abort_snapshot ();

        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Out_of_ram)
      {
        Genode::error ("Snapper is out of RAM! Aborting the snapshot!");
        __abort_snapshot ();

        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Out_of_caps)
      {
        Genode::error (
            "Snapper is out of capabilities! Aborting the snapshot!");
        __abort_snapshot ();

        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Denied)
      {
        Genode::error ("Memory allocation denied! Aborting the snapshot!");
        __abort_snapshot ();

        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }

    if (snapper_config.verbose)
      Genode::log ("Generation committed successfully!");

    state = Dormant;
    return Ok;
  }

  /*
   * HELPER FUNCTIONS
   */

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
        if (!snapper_root.directory_exists (old))
          {
            snapper_root.unlink (latest);
            if (snapper_root.directory_exists (latest))
              {
                Genode::error ("Could not remove old generation: ", latest);
                return InitFailed;
              }
          }
      }

    if (snapper_config.verbose)
      Genode::log ("No unfinished generation remain.");

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
        Genode::error ("Could not create generation directory: ", timestamp);
        return InitFailed;
      }

    generation.construct (snapper_root, timestamp);

    generation->create_sub_directory ("snapshot");
    if (!generation->directory_exists ("snapshot"))
      {
        Genode::error ("Could not create snapshot directory: ", timestamp,
                       "/snapshot");
        return InitFailed;
      }

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> latest = "";

    snapshot.construct (*generation, Genode::Path<11> ("snapshot"));
    snapshot_dir_path = Genode::Directory::join (latest, "snapshot");

    return Ok;
  }

  Snapper::Result
  Snapper::__load_gen (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation)
  {
    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> latest
        = generation;

    if (latest == "")
      {
        TODO ("check whether archive file has a valid crc");
        snapper_root.for_each_entry (
            [this, &latest] (Genode::Directory::Entry &e) {
              if (latest == "" || e.name () > latest)
                {
                  if (snapper_root.file_exists (
                          Genode::Directory::join (e.name (), "archive")))
                    {
                      latest = e.name ();
                    }
                }
            });
      }

    if (latest == "")
      return Ok;

    try
      {
        Genode::Readonly_file archive_file (
            snapper_root, Genode::Directory::join (latest, "archive"));

        TODO ("read and check version");
        TODO ("read and check crc");

        Genode::Readonly_file::At pos (5);

        char _key_buf[sizeof (Archive::ArchiveKey)];
        char _val_buf[Vfs::MAX_PATH_LEN];

        Genode::Byte_range_ptr key_buf (_key_buf, sizeof (_key_buf));
        Genode::Byte_range_ptr val_buf (_val_buf, sizeof (_val_buf));

        do
          {
            Genode::size_t bytes_read = archive_file.read (pos, key_buf);
            if (bytes_read == 0)
              {
                Genode::error ("Archive entry is invalid!");
                if (snapper_config.integrity)
                  {
                    throw CrashStates::INVALID_ARCHIVE_ENTRY;
                  }

                return LoadGenFailed;
              }

            pos.value += bytes_read;

            bytes_read = archive_file.read (pos, val_buf);
            if (bytes_read == 0)
              break;

            pos.value += bytes_read;

            Archive::ArchiveKey key
                = *(reinterpret_cast<Archive::ArchiveKey *> (key_buf.start));

            Archive::Backlink backlink ((char const *)val_buf.start);

            archiver->archive.with_element (
                key,
                [&backlink] (Archive::ArchiveEntry &entry) {
                  entry.queue.enqueue (backlink);
                },
                [this, &key, &backlink] () {
                  Genode::Fifo<Archive::Backlink> new_queue;
                  new_queue.enqueue (backlink);

                  Archive::ArchiveEntry _ (key, new_queue, archiver->archive);
                });
          }
        while (true);
      }
    catch (Genode::File::Open_failed)
      {
        Genode::error ("Failed to open archive file of generation: ", latest);

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

} // namespace SnapperNS
