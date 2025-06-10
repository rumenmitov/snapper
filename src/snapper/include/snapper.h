#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
namespace SnapperNS
{
  class Snapper;
  extern Snapper *snapper;

  class Snapper
  {
  public:
    Snapper () {}

    static Snapper *new_snapper ();

    void
    init_snapshot ()
    {
      // TODO
    }

    void
    take_snapshot ()
    {
      // TODO
    }

    void
    commit_snapshot ()
    {
      // TODO
    }

    void
    restore ()
    {
      // TODO
    }

    void
    purge ()
    {
      // TODO
    }

  private:
    Snapper (const Snapper &) = delete;
    Snapper operator= (Snapper &) = delete;

    static Snapper *instance;
  };

}

#endif // __cplusplus

#endif // __SNAPPER_H
