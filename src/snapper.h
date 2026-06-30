#ifndef __SNAPPER_H
#define __SNAPPER_H

#include <root/component.h>
#include <util/string.h>
#include <snapper_session/snapper_session.h>

namespace Snapper { struct Session_component; class Root_component; }


struct Snapper::Session_component : public Genode::Rpc_object<Session>
{
  /**
   * @brief Dataspace for communication with client.
   */
  Genode::Attached_ram_dataspace ds;

  Session_component () = delete;
  Session_component (Genode::Env &env, const Genode::Number_of_bytes bufsize)
      : ds (env.ram (), env.rm (), bufsize)
  {
  }

  Genode::Dataspace_capability
  _dataspace () override
  {
    return ds.cap ();
  }


  void create(void) override;
  void capture_ds(int chunkid, Genode::size_t) override;
  void abort(void) override;
  void commit(void) override;

  void load(int snapid) override;
  void restore_ds(int chunkid, Genode::size_t) override;
  void unload(void) override;

  void purge(int snapid) override;
  void purge_expired(void) override;

  void heal(void) override;
};


class Snapper::Root_component
  : public Genode::Root_component<Snapper::Session_component, Genode::Single_client>
{
private:
  Genode::Env &_env;
  Genode::Number_of_bytes _bufsize;  

protected:
  Create_result _create_session(const char *) override
  {
    return *new (md_alloc()) Snapper::Session_component(_env, _bufsize);
  }  
  
public:
  
  Root_component(Genode::Env &env, Genode::Allocator &md_alloc, const Genode::Number_of_bytes bufsize)
    : Genode::Root_component<Snapper::Session_component, Genode::Single_client>(env.ep(), md_alloc),
      _env(env), _bufsize(bufsize)
  {
    Genode::log ("root snapper component created");
  }

};




  
#endif // __SNAPPER_H
