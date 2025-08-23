#ifndef __SNAPPER_SESSION_CLIENT_H
#define __SNAPPER_SESSION_CLIENT_H

#include "snapper_session.h"
#include <base/rpc_client.h>

namespace Snapper
{
  class Session_client;
}

class Snapper::Session_client : public Genode::Rpc_client<Session>
{
private:

  using Local_rm = Genode::Local::Constrained_region_map;

  /**
   * Shared-memory buffer used for carrying the payload
   * of read/write operations
   */
  Genode::Attached_dataspace _io_buffer;

public:
  Session_client (Local_rm &local_rm, Genode::Capability<Session> cap)
    : Genode::Rpc_client<Session> (cap), _io_buffer(local_rm, call<Rpc_dataspace>())
  {
  }

  Genode::Dataspace_capability _dataspace (void) override
  {
    return call<Rpc_dataspace>();
  }

  Result _take_snapshot (Genode::size_t size, Archive::ArchiveKey identifier) override 
  {
    return call<Rpc_take_snapshot>(size, identifier);
  }

  Result _restore (Genode::size_t size, Archive::ArchiveKey identifier) override 
  {
    return call<Rpc_restore>(size, identifier);
  }

  Result
  init_snapshot (void) override
  {
    return call<Rpc_init_snapshot> ();
  }

  Result
  take_snapshot (void const *const payload, Genode::size_t size,
                 Archive::ArchiveKey identifier) override
  {
    // TODO should add a mutex guard here
    Genode::memcpy(_io_buffer.local_addr<void>(), payload, size);

    return call<Rpc_take_snapshot> (size, identifier);
  }

  Result
  commit_snapshot (void) override
  {
    return call<Rpc_commit_snapshot> ();
  }

  Result
  open_generation (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation
      = "") override
  {
    return call<Rpc_open_generation> (generation);
  }

  Result
  restore (void *dest, Genode::size_t size,
           Archive::ArchiveKey identifier) override
  {
    // TODO should add a mutex guard here
    Result res = call<Rpc_restore> (size, identifier);
    Genode::memcpy(dest, _io_buffer.local_addr<void>(), size);

    return res;
  }

  Result
  close_generation (void) override
  {
    return call<Rpc_close_generation> ();
  }

  Result
  purge (const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
             &generation
         = "") override
  {
    return call<Rpc_purge> (generation);
  }

  void
  purge_expired (void) override
  {
    call<Rpc_purge_expired> ();
  }
};

#endif // __SNAPPER_SESSION_CLIENT_H
