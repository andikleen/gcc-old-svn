/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite" } */
/* { dg-final { scan-assembler "ptwrite" } } */

struct foo
{
  __attribute__ ((vartrace)) int field;
};

struct foo a;

int
f (void)
{
  return a.field;
}
