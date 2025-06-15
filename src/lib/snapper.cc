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
      ts.second, ":", ts.microsecond);

  return str;
}

namespace SnapperNS
{

  Snapper *snapper = nullptr;
  Snapper *Snapper::instance = nullptr;

  Snapper::Snapper (Genode::Env &env)
      : env (env), config (env, "config"), heap (env.ram (), env.rm ()),
        snapper_root (env, heap, config.xml ().sub_node ("vfs")), rtc (env),
        generation (static_cast<Vfs::Simple_env &> (snapper_root)),
        snapshot (static_cast<Vfs::Simple_env &> (snapper_root)), archive ()
  {
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

  Snapper::Result
  Snapper::init_snapshot ()
  {
    if (state != Dormant)
      return InvalidState;

    state = Creation;

    // remove old, unfinished generation
    snapper_root.unlink ("current");
    if (snapper_root.directory_exists ("current"))
      {
        Genode::error ("Could not remove old generation: /current");
        return CouldNotRemoveDir;
      }

#ifdef VERBOSE
    Genode::log ("old, unfinished generations cleaned");
#endif // VERBOSE

    // create dir "current"
    snapper_root.create_sub_directory ("current");
    if (!snapper_root.directory_exists ("current"))
      {
        Genode::error ("Could not create directory: /current");

        return CouldNotCreateDir;
      }

    generation.construct (snapper_root, Genode::Path<10> ("current"));

    // create dir "current/snapshot"
    generation->create_sub_directory ("snapshot");
    if (!generation->directory_exists ("snapshot"))
      {
        Genode::error ("Could not create directory: /snapshot");
        return CouldNotCreateDir;
      }

    snapshot.construct (*generation, Genode::Path<11> ("snapshot"));

    // check if there is a prior generation and load its
    // archive

    archive.construct ();

    Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> latest = "";

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

            const Genode::size_t mapping_size = sizeof (Mapping);
            char _buf[mapping_size];

            Genode::Byte_range_ptr buf (_buf, mapping_size);

            Genode::Readonly_file::At start_of_archive_data (5);

            Genode::size_t bytes_read = 0;

            do
              {
                bytes_read += archive_file.read (start_of_archive_data, buf);
              }
            while (bytes_read < mapping_size);

            Mapping (*reinterpret_cast<Mapping *> (buf.start));
            TODO ("implement copy constructor for mapping, so that it can be "
                  "added to the dictionary");

            TODO ("repeat this until archive file is exhausted");
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

} // namespace SnapperNS
