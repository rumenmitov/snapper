#include <base/allocator.h>
#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"

namespace SnapperNS
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
        = new (snapper->heap) Archive::Backlink (val);

    backlink->_self = backlink;

    archive.with_element (
        key,
        [backlink] (Archive::ArchiveEntry &entry) {
          entry.queue.enqueue (*backlink);
        },
        [this, key, backlink] () {
          Archive::ArchiveEntry *entry
              = new (snapper->heap) Archive::ArchiveEntry (key, archive);

          entry->_self = entry;

          entry->queue.enqueue (*backlink);
        });

    if (snapper->snapper_config.verbose)
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
        [] (Archive::ArchiveEntry &entry) {
          entry.queue.for_each ([] (Archive::Backlink &backlink) {
            Genode::destroy (snapper->heap, backlink._self);
          });

          Genode::destroy (snapper->heap, entry._self);
        },
        [key] () {
          if (snapper->snapper_config.verbose)
            {
              Genode::warning ("no such key exists in archive: ", key);
            }
        });

    if (snapper->snapper_config.verbose)
      {
        Genode::log ("archive entry removed: ", key);
      }
  }
}
