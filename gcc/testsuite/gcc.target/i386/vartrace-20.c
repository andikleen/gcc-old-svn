/* { dg-do compile } */
/* { dg-options "-fvartrace=reads -mptwrite -fdump-tree-vartrace" } */

int global;

/* { dg-final { scan-tree-dump-times "(?n)fswitch.*insert.*ptwrite" 1 "vartrace" } }*/
int fswitch(void)
{
  switch (global)
    {
    case 1:
      return 1;
    default:
      return 0;
    }
}

/* { dg-final { scan-tree-dump-times "(?n)fif.*insert.*ptwrite" 1 "vartrace" } }*/
int fif(void)
{
  if (global > 0)
    return 1;
  return 0;
}

int (*fptr)(void);

/* { dg-final { scan-tree-dump-times "(?n)findcall.*insert.*ptwrite" 1 "vartrace" } }*/
int findcall(void)
{
  return fptr();
}
