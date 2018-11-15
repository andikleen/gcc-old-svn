/* { dg-do compile } */
/* { dg-options "-fvartrace=reads,writes -mptwrite" } */
/* { dg-final { scan-assembler-times "ptwrite" 5 } } */
/* Should be really 4 times, but so far still one redundant ptwrite */

int global;
int global2;

void f1(int i)
{
  asm("nop" :: "r" (i) : "memory");
}

void f2(void)
{
  asm("nop" : "=m" (global) :: "memory");
}

void f3(void)
{
  asm("nop" : "=m" (global) : "r" (global2) : "memory");
}
