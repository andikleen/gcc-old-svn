/* { dg-do compile { target i?86-*-* x86_64-*-* } } */
/* { dg-options "-O2 -mptwrite -fvartrace " } */
/* { dg-final { scan-assembler "ptwrite" } } */

typedef int a;
enum b
{ };
struct ac
{
  a operator () (a, a, a, a, a, a);
};
struct c
{
  ac ag;
} extern ai[];
a d;
void
l (a e)
{
  b f;
  a g, h, i, j, k;
  e = d;
  ai[f].ag (e, g, h, i, j, k);
}
