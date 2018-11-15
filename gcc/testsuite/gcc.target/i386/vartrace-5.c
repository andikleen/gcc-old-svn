/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace=writes" } */
/* { dg-final { scan-assembler "ptwrite" } } */

int a;

void
f (void)
{
  a++;
}
