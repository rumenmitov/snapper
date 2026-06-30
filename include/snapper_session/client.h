/**
 * @brief Client-side Snapper interface.
 * @author Rumen Mitov
 * @date 2025-08-23
 */

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
   * @brief Shared-memory buffer used for carrying the payload
   *        of read/write operations
   */
  Genode::Attached_dataspace _io_buffer;

  /**
   * @brief Mutex guard for _io_buffer.
   */
  Genode::Mutex _mutex;

public:
  Session_client (Local_rm &local_rm, Genode::Capability<Session> cap)
      : Genode::Rpc_client<Session> (cap),
        _io_buffer (local_rm, call<Rpc_dataspace> ()), _mutex ()
  {
  }

  Genode::Dataspace_capability
  _dataspace (void) override
  {
    return call<Rpc_dataspace> ();
  }

  void
  create (void) override
  {
    return call<Rpc_create> ();
  }

  /* INFO
     Client-side only function. 'payload' is copied to the
     dataspace which can then be used by capture_ds().
  */
  void
  capture (int chunkid, Genode::Span const &payload)
  {
    Genode::Mutex::Guard _guard (_mutex);
    
    Genode::memcpy (_io_buffer.local_addr<void> (),
                    payload.start,
                    payload.num_bytes);
    
    return call<Rpc_capture_ds> (chunkid, size);
  }

  /* INFO
     Necessary only for the Snapper server.
   */
  void
  capture_ds (int, Genode::size_t) = 0;

  void
  abort (void) override
  {
    return call<Rpc_abort> ();
  }

  void
  commit (void) override
  {
    return call<Rpc_commit> ();
  }

  void
  load (int snapid) override
  {
    return call<Rpc_load> (snapid);    
  }

  /* INFO
     Client-side only function. 'payload' is copied to the
     dataspace which can then be used by capture_ds().
  */
  void
  restore (int chunkid, Genode::Byte_range_ptr const &payload)
  {
    Genode::Mutex::Guard _guard (_mutex);
    
    call<Rpc_restore_ds> (chunkid, payload.num_bytes);
    Genode::memcpy (payload.start, _io_buffer.local_addr<void> (), payload.size);
  }

  /* INFO
     Necessary only for the Snapper server.
   */
  void
  restore_ds (int chunkid, Genode::size_t size) = 0;

  void
  unload (void) override
  {
    return call<Rpc_unload> ();
  }

  void
  purge (int snapid) override
  {
    return call<Rpc_purge> (snapid);
  }

  void
  purge_expired (void) override
  {
    call<Rpc_purge_expired> ();
  }

  void
  heal (void) override
  {
    return call<Rpc_heal> ();
  }
};

#endif // __SNAPPER_SESSION_CLIENT_H
