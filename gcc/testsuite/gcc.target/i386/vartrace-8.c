/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite" } */
/* { dg-final { scan-assembler-times "ptwrite" 1 } } */

int a;

__attribute__ ((vartrace))
     int f (void)
{
  return a;
}
