/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite -fvartrace" } */
/* { dg-final { scan-assembler-not "ptwrite" } } */

struct foo 
{
  int __attribute__((no_vartrace)) field;
};

struct foo a;

extern void f2(int);

int f(void)
{
  f2 (a.field);
}
