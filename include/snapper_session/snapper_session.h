/**
 * @brief Snapper session and root interfaces.
 * @author Rumen Mitov
 * @date 2025-08-23
 */

#ifndef __SNAPPER_SESSION_H
#define __SNAPPER_SESSION_H

#ifdef __cplusplus
#include <base/attached_ram_dataspace.h>
#include <base/rpc.h>
#include <session/session.h>

#include "snapper.h"

namespace Snapper
{
  struct Session;
  struct Session_component;
  class Root_component;
};

using namespace Snapper;

struct Snapper::Session : Genode::Session
{
  static const char *
  service_name ()
  {
    return "Snapper";
  }

  // TODO Implement this in a better way.
  static const Genode::size_t BUFSIZE = 1024 * 1024;

  /*
   * A terminal session consumes a dataspace capability for the server's
   * session-object allocation, its session capability, and a dataspace
   * capability for the communication buffer.
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

  /**
   * @brief Internal wrapper that uses the communication buffer.
   */
  virtual Result _take_snapshot (Genode::size_t, Archive::ArchiveKey) = 0;

  /**
   * @brief Internal wrapper that uses the communication buffer.
   */
  virtual Result _restore (Genode::size_t, Archive::ArchiveKey) = 0;

  virtual Result init_snapshot (void) = 0;

  virtual Result commit_snapshot (void) = 0;

  virtual Result open_generation (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          & = "")
      = 0;

  virtual Result close_generation (void) = 0;

  virtual Result
  purge (const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
             & = "")
      = 0;

  virtual void purge_expired (void) = 0;

  GENODE_RPC (Rpc_dataspace, Genode::Dataspace_capability, _dataspace);

  GENODE_RPC (Rpc_init_snapshot, Result, init_snapshot);

  GENODE_RPC (Rpc_take_snapshot, Result, _take_snapshot, Genode::size_t,
              Archive::ArchiveKey);

  GENODE_RPC (Rpc_commit_snapshot, Result, commit_snapshot);

  GENODE_RPC (
      Rpc_open_generation, Result, open_generation,
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> &);

  GENODE_RPC (Rpc_restore, Result, _restore, Genode::size_t,
              Archive::ArchiveKey);

  GENODE_RPC (Rpc_close_generation, Result, close_generation);

  GENODE_RPC (
      Rpc_purge, Result, purge,
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> &);

  GENODE_RPC (Rpc_purge_expired, void, purge_expired);

  GENODE_RPC_INTERFACE (Rpc_dataspace, Rpc_init_snapshot, Rpc_take_snapshot,
                        Rpc_commit_snapshot, Rpc_open_generation, Rpc_restore,
                        Rpc_close_generation, Rpc_purge, Rpc_purge_expired);
};

struct Snapper::Session_component : Genode::Rpc_object<Session>
{
  /**
   * @brief Session_component is a wrapper for the Snapper::Main object.
   */
  Snapper::Main &snapper;

  /**
   * @brief Dataspace for communication with client.
   */
  Genode::Attached_ram_dataspace ds;

  Session_component () = delete;
  Session_component (Genode::Env &env, Snapper::Main &snapper)
      : snapper (snapper), ds (env.ram (), env.rm (), BUFSIZE)
  {
  }

  Genode::Dataspace_capability
  _dataspace () override
  {
    return ds.cap ();
  }

  Result
  _take_snapshot (Genode::size_t size, Archive::ArchiveKey identifier) override
  {
    return snapper.take_snapshot (ds.local_addr<void> (), size, identifier);
  }

  Result
  _restore (Genode::size_t size, Archive::ArchiveKey identifier) override
  {
    return snapper.restore (ds.local_addr<void> (), size, identifier);
  }

  Result
  init_snapshot (void) override
  {
    return snapper.init_snapshot ();
  }

  Result
  commit_snapshot (void) override
  {
    return snapper.commit_snapshot ();
  }

  Result
  open_generation (
      const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
          &generation) override
  {
    return snapper.open_generation (generation);
  }

  Result
  close_generation (void) override
  {
    return snapper.close_generation ();
  }

  Result
  purge (const Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
             &generation) override
  {
    return snapper.purge (generation);
  }

  void
  purge_expired (void) override
  {
    snapper.purge_expired ();
  }
};
// TODO Update the Root component code to the newer Genode API when it
// comes out on https://depot.genode.org (NB: update schedule is tied
// to SculptOS).
class Snapper::Root_component
    : public Genode::Root_component<Session_component>
{
protected:
  Session_component *
  _create_session (const char *) override
  {
    return new (md_alloc ()) Session_component (env, snapper);
  }

public:
  Root_component (Genode::Env &env, Genode::Entrypoint &ep,
                  Genode::Allocator &md_alloc, Snapper::Main &snapper)
      : Genode::Root_component<Session_component> (ep, md_alloc), env (env),
        snapper (snapper)
  {
    if (snapper.config.verbose)
      Genode::log ("root snapper component created");
  }

private:
  Genode::Env &env;
  Snapper::Main &snapper;
};

#endif // __cplusplus

#endif // __SNAPPER_SESSION_H
