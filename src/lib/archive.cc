#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"

namespace Snapper
{
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

    if (snapper.config.verbose)
      {
        Genode::log ("archive entry inserted: ", key, " -> \"",
                     backlink->value, "\"");
      }
  }

  void
  Snapper::Archive::remove (const ArchiveKey key)
  {
    archive.with_element (
        key,
        [this] (Archive::ArchiveEntry &entry) {
          entry.queue.for_each ([this] (Archive::Backlink &backlink) {
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
