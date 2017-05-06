/* { dg-do compile { target i?86-*-* x86_64-*-* } } */
/* { dg-options "-O2 -mptwrite -fvartrace-args " } */
/* { dg-final { scan-assembler "ptwrite" } } */

int a;
int b(int c) 
{
  if (a)
    c = 0;
  else
    c = b(a);
  b(c);
}
