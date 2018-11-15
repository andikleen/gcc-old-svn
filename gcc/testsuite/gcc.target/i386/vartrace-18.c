/* { dg-do compile } */
/* { dg-options "-fvartrace=reads -mptwrite" } */
/* { dg-final { scan-assembler-times "ptwrite" 2 } } */
/* Source: Martin Sebor */

int global __attribute__((no_vartrace));
int array[10];

int f(void)
{
  return array[global];
}

int array2[10] __attribute__((no_vartrace));
int global2;

int f2(void)
{
  return array2[global2];
}
