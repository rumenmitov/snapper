#include <util/construct_at.h>
#include <vfs/directory_service.h>
#include <vfs/vfs_handle.h>

#include "snapper.h"

namespace SnapperNS
{

  Snapper *snapper = nullptr;
  Snapper *Snapper::instance = nullptr;

  Snapper::Snapper (Genode::Env &env)
    : env (env), config(env, "config"), heap (env.ram (), env.rm ()),
      snapper_root (env, heap, config.xml().sub_node("vfs"))
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

    snapper_root.unlink ("current");
    if (snapper_root.directory_exists("current")) {
      Genode::error("Could not remove old generation: ", snapper_root.Path, "/current");
      return CouldNotRemoveDir;
    }

#ifdef VERBOSE
    Genode::log ("old, unfinished generations cleaned");
#endif // VERBOSE

    // create dir "current"
    snapper_root.create_sub_directory("current");
    if (!snapper_root.directory_exists("current")) {
      Genode::error("Could not create directory: ", snapper_root.Path, "/current");
      return CouldNotCreateDir;
    }

    generation(snapper_root, "current");
    
    // create dir "current/snapshot"
    generation.create_sub_directory("snapshot");
    if (!generation.directory_exists("snapshot")) {
      Genode::error("Could not create directory: ", generation.Path, "/snapshot");
      return CouldNotCreateDir;
    }

    snapshot(generation, "snapshot");

    // check if there is a prior generation, if there is load the
    // archive

    return Ok;
  }

} // namespace SnapperNS
