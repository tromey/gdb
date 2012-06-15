#include <new>
#include <cstdlib>

// For convenience when debugging gdb.
void *last_alloc;

int counter;

// Our own global operator new which updates counter.
void *operator new (std::size_t size)
{
  ++counter;
  return last_alloc = malloc (size);
}

void *operator new[] (std::size_t size)
{
  ++counter;
  return last_alloc = malloc (size);
}

void operator delete (void *ptr)
{
  --counter;
  free (ptr);
}

void operator delete[] (void *ptr)
{
  --counter;
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

static int other_counter;

struct HasOps
{
  int x;

  void *operator new (std::size_t size)
  {
    ++other_counter;
    return last_alloc = malloc (size);
  }

  void *operator new (std::size_t size, void *ptr) throw()
  {
    return ptr;
  }

  void *operator new (std::size_t size, int x, int y) throw()
  {
    other_counter += x;
    return last_alloc = malloc (size);
  }

  void *operator new[] (std::size_t size)
  {
    ++other_counter;
    return last_alloc = malloc (size);
  }

  void operator delete (void *ptr)
  {
    --other_counter;
    free (ptr);
  }

  void operator delete[] (void *ptr)
  {
    --other_counter;
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

  virtual ~Base()
  {
  }
};

int derived_count;

struct DerivedFromBase : public Base
{
  void operator delete (void *ptr)
  {
    ++derived_count;
    free (ptr);
  }

  ~DerivedFromBase()
  {
  }
};

int keep_stuff ()
{
  delete new HasOps;
  delete[] new HasOps[5];
  delete new DerivedFromBase;
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

  // Reset for the test suite.
  counter = 0;
  other_counter = 0;
  derived_count = 0;

  return 0;			// Stop here
}
