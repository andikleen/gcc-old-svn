/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace -fvartrace=locals" } */
/* { dg-final { scan-assembler "ptwrite" } } */

int foo;

extern void f2 (void);

void
f0 (void)
{
  foo += 1;
}

int
f3 (int a)
{
  return a * 2;
}

extern void extfunc (int);

int
f4 (int a, int b)
{
  extfunc (a);
  extfunc (b);
  return a + b;
}

void
f5 (int a)
{
}

int
f (int a, int b)
{
  f2 ();
  return a + b + foo;
}
