#include <new>
#include <cstdlib>

// For convenience when debugging gdb.
void *last_alloc;

enum what_operator
  {
    WHATOP_INVALID = 0,

    WHATOP_GLOBAL = 2,
    WHATOP_HASOPS = 4,
    WHATOP_DERIVED = 8,

    WHATOP_ARRAY = 16,
    WHATOP_DELETE = 32,
    WHATOP_PLACEMENT = 64,
    WHATOP_ARGS = 128
  };

int whatop = WHATOP_INVALID;

void *operator new (std::size_t size)
{
  whatop = WHATOP_GLOBAL;
  return last_alloc = malloc (size);
}

void *operator new[] (std::size_t size)
{
  whatop = WHATOP_GLOBAL | WHATOP_ARRAY;
  return last_alloc = malloc (size);
}

void operator delete (void *ptr)
{
  whatop = WHATOP_GLOBAL | WHATOP_DELETE;
  free (ptr);
}

void operator delete[] (void *ptr)
{
  whatop = WHATOP_GLOBAL | WHATOP_ARRAY | WHATOP_DELETE;
  free (ptr);
}

struct Simple
{
  int x;
  Simple() : x (7) { }
  Simple (int y) : x (y) { }
};

struct Derived : public Simple
{
};

struct HasOps
{
  int x;

  void *operator new (std::size_t size)
  {
    whatop = WHATOP_HASOPS;
    return last_alloc = malloc (size);
  }

  void *operator new (std::size_t size, void *ptr) throw()
  {
    whatop = WHATOP_HASOPS | WHATOP_PLACEMENT;
    return ptr;
  }

  void *operator new (std::size_t size, int x, int y) throw()
  {
    whatop = WHATOP_HASOPS | WHATOP_ARGS;
    return last_alloc = malloc (size);
  }

  void *operator new[] (std::size_t size)
  {
    whatop = WHATOP_HASOPS | WHATOP_ARRAY;
    return last_alloc = malloc (size);
  }

  void operator delete (void *ptr)
  {
    whatop = WHATOP_HASOPS | WHATOP_DELETE;
    free (ptr);
  }

  void operator delete[] (void *ptr)
  {
    whatop = WHATOP_HASOPS | WHATOP_DELETE | WHATOP_ARRAY;
    free (ptr);
  }

  HasOps () : x (7) { }
  HasOps (int y) : x (y) { }
};

template<typename T>
struct WithConstructor
{
  T x;

  WithConstructor (T y) : x (y) { }
};

namespace Name
{
  template<typename T>
  struct InNameSpace
  {
    T x;

    InNameSpace (T y) : x (y) { }
  };
}

struct Base
{
  void operator delete (void *ptr)
  {
    free (ptr);
  }

  Base()
  {
  }

  virtual ~Base()
  {
  }
};

struct DerivedFromBase : public Base
{
  void operator delete (void *ptr)
  {
    whatop = WHATOP_DERIVED | WHATOP_DELETE;
    free (ptr);
  }

  ~DerivedFromBase()
  {
  }
};

struct VDerived : public virtual Base
{
  VDerived () : Base()
  {
  }

  ~VDerived()
  {
  }
};

struct VDerived2 : public VDerived, public virtual Base
{
  VDerived2 () : VDerived (), Base ()
  {
  }

  ~VDerived2()
  {
  }
};

int keep_stuff ()
{
  delete new HasOps;
  delete[] new HasOps[5];
  delete (Base *) new DerivedFromBase;
  delete new Base;
  delete new VDerived;
  delete new VDerived2;
}

int main ()
{
  keep_stuff ();

  int *ip = new int;
  int *ip2 = new int (5);

  int **y = new int *;
  int **y2 = new int*[5];

  int (*z)[7] = new int[5][7];

  Simple *s = new Simple;
  Simple *s2 = new Simple(23);
  Simple *s3 = new Simple[7];

  Derived *d = new Derived;

  HasOps *h = new HasOps;
  HasOps *h2 = new HasOps(23);
  HasOps *h3 = new HasOps[7];

  HasOps *h4 = (HasOps *) HasOps::operator new (sizeof (HasOps));
  h4 = new (h4) HasOps;

  HasOps *h5 = new (0, 88) HasOps(0);

  HasOps **hp = new HasOps *;

  WithConstructor<int> *w = new WithConstructor<int> (5);

  Name::InNameSpace<int> *ins = new Name::InNameSpace<int> (72);

  Base *b = new DerivedFromBase;

  return 0;			// Stop here
}
