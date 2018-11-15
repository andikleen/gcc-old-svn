/* { dg-do compile } */
/* { dg-options "-fvartrace=reads,writes -mptwrite" } */
/* { dg-final { scan-assembler-times "ptwrite" 7 } } */
/* XXX: should be 4 ptwrites, but right now we generate redundant ones */

int global;
int global2;

__attribute__((vartrace))
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
