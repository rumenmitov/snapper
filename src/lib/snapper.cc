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
  Snapper::Snapper (Genode::Env &env)
      : env (env), config (env, "config"), heap (env.ram (), env.rm ()),
        snapper_root (env, heap, config.xml ().sub_node ("vfs")), rtc (env),
        generation (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot_dir_path ("/"), archiver ()
  {
  }

  Snapper::~Snapper ()
  {
    generation.destruct ();
    snapshot.destruct ();
    archiver.destruct ();

    instance = nullptr;
    
#ifdef VERBOSE
    Genode::log ("snapper destroyed");
#endif // VERBOSE
  }

  Snapper *
  Snapper::new_snapper (Genode::Env &env)
  {
    if (Snapper::instance)
      return Snapper::instance;

    static Snapper local_snapper (env);
    env.exec_static_constructors ();

    Snapper::instance = &local_snapper;

#ifdef VERBOSE
    Genode::log ("new snapper created");
#endif // VERBOSE

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

    // check if latest generation is valid. if not, remove it
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
                return CouldNotRemoveDir;
              }
          }
      }

#ifdef VERBOSE
    Genode::log ("old, unfinished generations cleaned");
#endif // VERBOSE

    // create generation directory
    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> timestamp
        = timestamp_to_str (rtc.current_time ());

    snapper_root.create_sub_directory (timestamp);
    if (!snapper_root.directory_exists (timestamp))
      {
        Genode::error ("Could not create generation directory: ", timestamp);
        return CouldNotCreateDir;
      }

    generation.construct (snapper_root, timestamp);

    // create dir "current/snapshot"
    generation->create_sub_directory ("snapshot");
    if (!generation->directory_exists ("snapshot"))
      {
        Genode::error ("Could not create snapshot directory: ", timestamp,
                       "/snapshot");
        return CouldNotCreateDir;
      }

    snapshot.construct (*generation, Genode::Path<11> ("snapshot"));
    snapshot_dir_path = Genode::Directory::join (latest, "snapshot");

    // check if there is a prior generation and load its
    // archive file
    latest = "";

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

    if (latest != "")
      {
        try
          {
            Genode::Readonly_file archive_file (
                snapper_root, Genode::Directory::join (latest, "archive"));

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
                    TODO ("handle invalid archive entry");
                    Genode::error ("archive entry is invalid");
                    break;
                  }

                pos.value += bytes_read;

                bytes_read = archive_file.read (pos, val_buf);
                if (bytes_read == 0)
                  break;

                pos.value += bytes_read;

                Archive::ArchiveElement _ (
                  *(reinterpret_cast<Archive::ArchiveKey *> (key_buf.start)),
                    val_buf.start, archiver->archive);
              }
            while (true);
          }
        catch (Genode::File::Open_failed)
          {
            Genode::error ("Failed to open archive file of generation: ",
                           latest);
            TODO ("handle recovery is not possible");
          }
      }

    return Ok;
  }

  Snapper::Result
  Snapper::take_snapshot (void const *const payload, Genode::uint64_t size,
                          Archive::ArchiveKey identifier)
  {
    if (state != Creation)
      return InvalidState;

    bool new_file_needed = false;
    Genode::uint8_t reference_count = 0;
    Genode::uint32_t crc = 0;

    TODO ("calculate crc of payload");

    // check if identifier exists in the mapping and if the crc
    // matches the calculated crc of the payload.
    archiver->archive.with_element (
        identifier,
        [this, &new_file_needed] (const Archive::ArchiveElement &element) {
          try
            {
              Genode::Readonly_file file (snapper_root, element.value);

              TODO ("read crc and rc of file and decide whether it needs to "
                    "be updated");
            }
          catch (Genode::File::Open_failed)
            {
              new_file_needed = true;

#ifdef VERBOSE
              Genode::log ("Failed to open snapshot file: ", element.value);
#endif // VERBOSE
            }
        },
        [&new_file_needed] () { new_file_needed = true; });

    if (!new_file_needed)
      return Ok;

    // create a new snapshot file and write to it the payload metadata
    // and the payload data
    if (snapshot_file_count >= SNAPPER_THRESH)
      {
        snapshot->create_sub_directory ("ext");
        if (!snapshot->directory_exists ("ext"))
          {
            Genode::error ("Could not create extender sub-directory!");
            TODO ("handle snapshot not possible!");
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
            TODO ("handle snapshot not possible!");
          }

        // writing reference count
        res = file.append ((char *)&reference_count,
                           sizeof (decltype (reference_count)));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Could not write reference count to file: ",
                           filepath_base);
            TODO ("handle snapshot not possible!");
          }

        // writing crc
        res = file.append ((char *)&crc, sizeof (decltype (crc)));

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Could not write CRC to file: ", filepath_base);

            TODO ("handle snapshot not possible!");
          }

        // writing payload
        res = file.append ((char *)&payload, size);

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("Could not write payload to file: ", filepath_base);
            TODO ("handle snapshot not possible!");
          }
      }
    catch (Genode::New_file::Create_failed)
      {
        Genode::error ("Could not create file: ", filepath_base);
        TODO ("handle snapshot not possible!");
      }

    // save the snapshot file's path into the archive (relative to
    // snapper_root)

    Genode::String<Vfs::MAX_PATH_LEN> filepath
        = Genode::Directory::join (snapshot_dir_path, filepath_base);

    archiver->archive.with_element (
        identifier,
        [filepath] (Archive::ArchiveElement &value) { value.value = filepath; },
        [this, identifier, filepath] () {
          Archive::ArchiveElement _ (identifier, filepath.string (), archiver->archive);
        });

    return Ok;
  }

} // namespace SnapperNS
