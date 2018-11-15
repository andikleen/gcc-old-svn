/* { dg-do compile } */
/* { dg-options "-fvartrace=reads -mptwrite" } */
/* { dg-final { scan-assembler-times "ptwrite" 2 } } */

int global;

int f1(void)
{
  switch (global)
    {
    case 1:
      return 1;
    default:
      return 0;
    }
}

int f2(void)
{
  if (global > 0)
    return 1;
  return 0;
}
