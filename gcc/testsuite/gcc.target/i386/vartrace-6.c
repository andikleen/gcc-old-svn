/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace=reads -fvartrace=locals" } */
/* { dg-final { scan-assembler "ptwrite" } } */

extern void f2 (void);

void
f (void)
{
  int i;
  for (i = 0; i < 10; i++)
    f2 ();
}
