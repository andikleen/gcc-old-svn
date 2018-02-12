/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite" } */
/* { dg-final { scan-assembler "ptwrite" } } */

void ptwrite1(int a)
{
  __builtin_ia32_ptwrite32 (a);
}

void ptwrite2(unsigned long b)
{
  __builtin_ia32_ptwrite64 (b);
}

void ptwrite3(unsigned char b)
{
  __builtin_ia32_ptwrite64 (b);
}

void ptwrite4(unsigned short b)
{
  __builtin_ia32_ptwrite64 (b);
}

void ptwrite5(unsigned short b)
{
  __builtin_ia32_ptwrite32 (b);
}
