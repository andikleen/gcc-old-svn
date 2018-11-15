/* { dg-do compile } */
/* { dg-options "-fvartrace -mptwrite -fdump-tree-vartrace" } */

/* { dg-final { scan-tree-dump-times "(?n)ffloat.*insert.*ptwrite" 2 "vartrace" } }*/
float ffloat(float x)
{
  return x + 1;
}

/* { dg-final { scan-tree-dump-times "(?n)fdouble.*insert.*ptwrite" 2 "vartrace" } }*/
double fdouble(double x)
{
  return x + 1;
}

/* { dg-final { scan-tree-dump-times "(?n)fchar.*insert.*ptwrite" 2 "vartrace" } }*/
char fchar(char x)
{
  return x + 1;
}

/* { dg-final { scan-tree-dump-times "(?n)fshort.*insert.*ptwrite" 2 "vartrace" } }*/
short fshort(short x)
{
  return x + 1;
}

/* { dg-final { scan-tree-dump-times "(?n)fuchar.*insert.*ptwrite" 2 "vartrace" } }*/
unsigned char fuchar(unsigned char x)
{
  return x + 1;
}

/* { dg-final { scan-tree-dump-times "(?n)fushort.*insert.*ptwrite" 2 "vartrace" } }*/
unsigned short fushort(unsigned short x)
{
  return x + 1;
}
