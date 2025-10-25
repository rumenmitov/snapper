#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"
#include "utils.h"

namespace Snapper
{

  Snapper::Archive::Archive (Snapper::Main &snapper)
      : archive (), snapper (snapper)
  {
  }

  Snapper::Archive::~Archive ()
  {
    while (true)
      {
        bool not_empty = archive.with_any_element (
            [this] (const Archive::ArchiveEntry &entry) {
              remove (entry.name);
            });

        if (!not_empty)
          break;
      }

    total_backlinks = 0;
  }

  void
  Snapper::Archive::insert (const Archive::ArchiveKey key,
                            const Genode::String<Vfs::MAX_PATH_LEN> &val)
  {
    Snapper::Archive::Backlink *backlink
        = new (snapper.heap) Archive::Backlink (*this, val);

    backlink->_self = backlink;

    archive.with_element (
        key,
        [backlink] (Archive::ArchiveEntry &entry) {
          entry.queue.enqueue (*backlink);
        },
        [this, key, backlink] () {
          Archive::ArchiveEntry *entry
              = new (snapper.heap) Archive::ArchiveEntry (key, archive);

          entry->_self = entry;

          entry->queue.enqueue (*backlink);
        });

    total_backlinks++;

    if (snapper.config.verbose)
      {
        Genode::log ("archive entry inserted: ", key, " -> \"",
                     backlink->value, "\"");
      }
  }

  void
  Snapper::Archive::commit (Genode::Directory &dir,
                            const Genode::String<Vfs::MAX_PATH_LEN> &file)
  {
    if (dir.file_exists (file))
      {
        Genode::error (
            "archive commit to already existing file is not allowed!");
        throw Snapper::CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }

    try
      {
        Genode::New_file archive_file (dir, file);

        constexpr Genode::size_t key_size = sizeof (Archive::ArchiveKey);

        constexpr Genode::size_t val_size
            = sizeof (decltype (Archive::Backlink::value));

        constexpr Genode::size_t kv_pair_size = key_size + val_size;

        const Genode::size_t archive_data_size
            = total_backlinks * kv_pair_size;
        char *archive_data_buf = new (snapper.heap) char[archive_data_size];

        Genode::uint64_t idx = 0;

        archive.for_each ([&archive_data_buf,
                           &idx] (const Archive::ArchiveEntry &entry) {
          entry.queue.for_each ([archive_data_buf, &idx,
                                 &entry] (const Archive::Backlink &backlink) {
            Genode::memcpy (archive_data_buf + (idx * kv_pair_size),
                            &entry.name, key_size);

            Genode::memcpy (archive_data_buf + (idx * kv_pair_size + key_size),
                            backlink.value.string (), val_size);

            idx++;
          });
        });

        const Genode::size_t archive_buf_size
            = sizeof (Snapper::VERSION) + sizeof (Snapper::CRC)
              + sizeof (decltype (total_backlinks)) + archive_data_size;

        char *archive_buf = new (snapper.heap) char[archive_buf_size];

        Snapper::VERSION ver = Version;
        Snapper::CRC crc = crc32 (archive_data_buf, archive_data_size);

        Genode::memcpy (archive_buf, &ver, sizeof (Snapper::VERSION));

        Genode::memcpy (archive_buf + sizeof (Snapper::VERSION), &crc,
                        sizeof (Snapper::CRC));

        Genode::memcpy (archive_buf + sizeof (Snapper::VERSION)
                            + sizeof (Snapper::CRC),
                        &total_backlinks, sizeof (decltype (total_backlinks)));

        Genode::memcpy (archive_buf + sizeof (Snapper::VERSION)
                            + sizeof (Snapper::CRC)
                            + sizeof (decltype (total_backlinks)),
                        archive_data_buf, archive_data_size);

        snapper.heap.free (archive_data_buf, archive_data_size);

        Genode::New_file::Append_result res
            = archive_file.append (archive_buf, archive_buf_size);

        snapper.heap.free (archive_buf, archive_buf_size);

        if (res != Genode::New_file::Append_result::OK)
          {
            Genode::error ("failed to write to the archive file!");
            throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
          }
      }
    catch (Genode::New_file::Create_failed)
      {
        Genode::error ("failed to create archive file!");
        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Out_of_ram)
      {
        Genode::error ("snapper is out of RAM!");
        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Out_of_caps)
      {
        Genode::error ("snapper is out of capabilities!");
        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
    catch (Genode::Denied)
      {
        Genode::error ("memory allocation denied!");
        throw CrashStates::SNAPSHOT_NOT_POSSIBLE;
      }
  }

  void
  Snapper::Archive::remove (const ArchiveKey key)
  {
    archive.with_element (
        key,
        [this] (Archive::ArchiveEntry &entry) {
          entry.queue.for_each ([this] (Archive::Backlink &backlink) {
            total_backlinks--;
            Genode::destroy (snapper.heap, backlink._self);
          });

          Genode::destroy (snapper.heap, entry._self);
        },
        [key, this] () {
          if (snapper.config.verbose)
            {
              Genode::warning ("no such key exists in archive: ", key);
            }
        });

    if (snapper.config.verbose)
      {
        Genode::log ("archive entry removed: ", key);
      }
  }
}
