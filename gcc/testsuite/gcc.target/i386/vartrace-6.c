/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace=reads,locals" } */
/* { dg-final { scan-assembler "ptwrite" } } */

extern void f2 (int);

void
f (void)
{
  int i;
  for (i = 0; i < 10; i++)
    f2 (i);
}
