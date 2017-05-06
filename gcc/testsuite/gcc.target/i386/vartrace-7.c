/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite" } */
/* { dg-final { scan-assembler "ptwrite" } } */

int a __attribute__ ((vartrace));

int
f (void)
{
  return a;
}
