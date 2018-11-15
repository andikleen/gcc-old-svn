/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace" } */
/* { dg-final { scan-assembler-not "ptwrite" } } */

int a __attribute__ ((no_vartrace));

extern void f2 (int);

void
f (void)
{
  f2 (a);
}
