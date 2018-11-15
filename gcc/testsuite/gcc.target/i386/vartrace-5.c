/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace=writes" } */
/* { dg-final { scan-assembler-times "ptwrite" 1 } } */

int a;

void
f (void)
{
  a++;
}
