/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite" } */
/* { dg-final { scan-assembler-times "ptwrite" 1 } } */

struct foo
{
  int field;
} __attribute__ ((vartrace));

struct foo a;

int
f (void)
{
  return a.field;
}
