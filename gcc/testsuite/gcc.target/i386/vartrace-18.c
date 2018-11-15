/* { dg-do compile } */
/* { dg-options "-fvartrace=reads -mptwrite -fdump-tree-vartrace" } */
/* Source: Martin Sebor */

int global __attribute__((no_vartrace));
int array[10];

/* { dg-final { scan-tree-dump-times "(?n)array.*insert.*ptwrite" 1 "vartrace" } } */
int trace_array(void)
{
  return array[global];
}

int array2[10] __attribute__((no_vartrace));
int global2;

/* { dg-final { scan-tree-dump-times "(?n)global.*insert.*ptwrite" 1 "vartrace" } } */
int trace_global(void)
{
  return array2[global2];
}
