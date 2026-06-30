/**
 * @brief Snapper session and root interfaces.
 * @author Rumen Mitov
 * @date 2025-08-23
 */

#ifndef __SNAPPER_SESSION_H
#define __SNAPPER_SESSION_H

#include <base/attached_ram_dataspace.h>
#include <base/rpc.h>
#include <session/session.h>


namespace Snapper
{
  struct Session;
};


struct Snapper::Session : Genode::Session
{
  static const char *
  service_name ()
  {
    return "Snapper";
  }

  /* INFO
     A session consumes a dataspace capability for the server's
     session-object allocation, its session capability, and a dataspace
     capability for the communication buffer.
   */
  enum
  {
    CAP_QUOTA = 3
  };

  /**
   * @brief Internal method for returning the dataspace used for the
   *        communication buffer.
   */
  virtual Genode::Dataspace_capability _dataspace (void) = 0;

  virtual void create (void)                    = 0;
  virtual void capture_ds (int, Genode::size_t) = 0;
  virtual void abort(void)                      = 0;
  virtual void commit (void)                    = 0;

  virtual void load(int)                               = 0;
  virtual void restore_ds(int chunkid, Genode::size_t) = 0;
  virtual void unload(void)                            = 0;

  virtual void purge(int)          = 0;
  virtual void purge_expired(void) = 0;

  virtual void heal(void) = 0;

  GENODE_RPC(Rpc_dataspace, Genode::Dataspace_capability, _dataspace);
  
  GENODE_RPC(Rpc_create, void, create);
  GENODE_RPC(Rpc_capture_ds, void, capture_ds, int, Genode::size_t);
  GENODE_RPC(Rpc_abort, void, abort);
  GENODE_RPC(Rpc_commit, void, commit);

  GENODE_RPC(Rpc_load, void, load, int);
  GENODE_RPC(Rpc_restore_ds, void, restore_ds, int, Genode::size_t);
  GENODE_RPC(Rpc_unload, void, unload);

  GENODE_RPC(Rpc_purge, void, purge, int);
  GENODE_RPC(Rpc_purge_expired, void, purge_expired);

  GENODE_RPC(Rpc_heal, void, heal);


  GENODE_RPC_INTERFACE (Rpc_dataspace, Rpc_create, Rpc_capture_ds, Rpc_abort, Rpc_commit,
                        Rpc_load, Rpc_restore_ds, Rpc_unload,
                        Rpc_purge, Rpc_purge_expired, Rpc_heal);
};


#endif // __SNAPPER_SESSION_H
