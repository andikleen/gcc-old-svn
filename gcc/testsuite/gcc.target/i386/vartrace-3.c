/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace=returns" } */
/* { dg-final { scan-assembler-times "ptwrite" 1 } } */

int
f (int a)
{
  return a;
}
