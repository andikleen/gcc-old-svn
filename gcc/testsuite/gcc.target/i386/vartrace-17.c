/* { dg-do compile } */
/* { dg-options "-fvartrace -mptwrite -fdump-tree-vartrace" } */

/* { dg-final { scan-tree-dump-times "(?n)read_args.*insert.*ptwrite" 3 "vartrace" } } */
int read_args(int a, int b)
{
  return a+b;
}

int x;

/* { dg-final { scan-tree-dump-times "(?n)global.*insert.*ptwrite" 5 "vartrace" } }*/
int global(int a)
{
  x += a;
  return x + a;
}

/* { dg-final { scan-tree-dump-times "(?n)pointer_ref.*insert.*ptwrite" 2 "vartrace" } }*/
int pointer_ref(int *f)
{
  return *f++;
}
