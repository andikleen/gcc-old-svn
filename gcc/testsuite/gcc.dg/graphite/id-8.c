/* { dg-options "-Os -fdump-tree-graphite-all" } */
int blah;
foo()
{
  int i;

  for (i=0 ; i< 7 ; i++)
    {
      if (i == 7 - 1)
	blah = 0xfcc;
      else
	blah = 0xfee;
    }
  return blah;
}
/* { dg-final { cleanup-tree-dump "graphite" } } */
