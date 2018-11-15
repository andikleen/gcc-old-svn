/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace=args" } */
/* { dg-final { scan-assembler "ptwrite" } } */

int
f (int a)
{
  return a;
}
