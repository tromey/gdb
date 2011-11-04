namespace A {
  namespace B {
    namespace C {
      int x = 5;

      int nsfunc () {
	return x;
      }
    }
  }
}

namespace Q = A::B;

int xfun() {
  return Q::C::x;		// Breakpoint xfun
}

using namespace Q::C;

int yfun() {
  return x;			// Breakpoint yfun
}

int main(){
  return xfun() + yfun();
}
