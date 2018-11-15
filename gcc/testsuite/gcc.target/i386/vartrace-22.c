/* { dg-do compile } */
/* { dg-options "-fvartrace=off -mptwrite" } */
/* { dg-final { scan-assembler-not "ptwrite" } } */

int foo;

__attribute__((vartrace)) void f(void)
{
  foo++;
}

int foo2 __attribute__((vartrace));

void f2(void)
{
  foo++;
}
