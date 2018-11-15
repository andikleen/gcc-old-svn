/* Test optimization for redundant PTWRITEs */
/* So far XFAIL because we generate redundant PTWRITEs */
/* { dg-do compile } */
/* { dg-options "-fvartrace -mptwrite" } */
/* { dg-final { scan-assembler-times "ptwrite" 8 { xfail *-*-* } } } */

int read_locals(int a, int b)
{
  return a+b;
}

int x;

int global(int a)
{
  x += a;
  return x + a;
}

int pointer_ref(int *f)
{
  return *f++;
}
