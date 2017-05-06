/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace" } */
/* { dg-final { scan-assembler-not "ptwrite" } } */

int a;

__attribute__ ((no_vartrace)) int f (void)
{
  return a;
}
