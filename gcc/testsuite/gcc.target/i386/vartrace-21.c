/* { dg-do compile } */
/* { dg-options "-fvartrace=reads,writes -mptwrite -fdump-tree-vartrace" } */

/* Expect a single ptwrite for each function */

/* { dg-final { scan-tree-dump-times "(?n)ptr_write.*insert.*ptwrite" 1 "vartrace" } }*/
void ptr_write (int *p)
{
  *p = 1;
}

/* { dg-final { scan-tree-dump-times "(?n)ptr_read.*insert.*ptwrite" 1 "vartrace" } }*/
int ptr_read (int *p)
{
  return *p;
}

/* { dg-final { scan-tree-dump-times "(?n)array_read.*insert.*ptwrite" 1 "vartrace" } }*/
int array_read (int *p, int index)
{
  return p[index];
}

/* { dg-final { scan-tree-dump-times "(?n)array_write.*insert.*ptwrite" 1 "vartrace" } }*/
int array_write (int *p, int index)
{
  p[index] = 1;
}

struct foo {
  int x;
};

/* { dg-final { scan-tree-dump-times "(?n)sfield_read.*insert.*ptwrite" 1 "vartrace" } }*/
int sfield_read(struct foo *p)
{
  return p->x;
}

/* { dg-final { scan-tree-dump-times "(?n)sfield_write.*insert.*ptwrite" 1 "vartrace" } }*/
void sfield_write(struct foo *p, int arg)
{
  p->x = arg;
}

union bar {
  int a;
  int b;
};

/* { dg-final { scan-tree-dump-times "(?n)union_read.*insert.*ptwrite" 1 "vartrace" } }*/

int union_read(union bar *p)
{
  return p->b;
}

/* { dg-final { scan-tree-dump-times "(?n)union_write.*insert.*ptwrite" 1 "vartrace" } }*/
void union_write(union bar *p, int arg)
{
  p->a = arg;
}

struct bf {
  unsigned f : 2;
};

/* { dg-final { scan-tree-dump-times "(?n)array_read.*insert.*ptwrite" 1 "vartrace" } }*/
int bitfield_read(struct bf *p)
{
  return p->f;
}

/* { dg-final { scan-tree-dump-times "(?n)bitfield_write.*insert.*ptwrite" 1 "vartrace" } }*/

void bitfield_write(struct bf *p, int arg)
{
  p->f = arg;
}
