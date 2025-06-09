#ifndef __SNAPPER_H
#define __SNAPPER_H

#ifdef __cplusplus
namespace Snapper
{
  class Snapper;
  extern Snapper *snapper;

  class Snapper
  {
  public:
    Snapper ();

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
    static Snapper *instance;
  };

}

#endif // __cplusplus

#endif // __SNAPPER_H

// Local Variables:
// mode: c++
// End:
