/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace-returns" } */
/* { dg-final { scan-assembler "ptwrite" } } */

int
f (int a)
{
  return a;
}
