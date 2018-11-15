/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace=reads" } */
/* { dg-final { scan-assembler-times "ptwrite" 1 } } */

int a;

extern void f2 (int);

int
f (void)
{
  f2 (a);
}
