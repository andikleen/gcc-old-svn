/* { dg-do compile { target { tail_call } } } */
/* { dg-options "-std=gnu++11" } */
/* { dg-additional-options "-fdelayed-branch" { target sparc*-*-* } } */

int f();

double h() { [[gnu::musttail]] return f(); } /* { dg-error "cannot tail-call" } */

template <class T>
__attribute__((noinline, noclone, noipa))
T g1() { [[gnu::musttail]] return f(); }

template <class T>
__attribute__((noinline, noclone, noipa))
T g2() { [[gnu::musttail]] return f(); } /* { dg-error "cannot tail-call" } */

template <class T>
__attribute__((noinline, noclone, noipa))
T g3() { [[gnu::musttail]] return f(); } /* { dg-error "cannot tail-call" } */
	
class C
{
  double x;
public:
  C(double x) : x(x) {}
  ~C() { asm("":::"memory"); }
};

int main()
{
  g1<int>();
  g2<double>();
  g3<C>();
}
