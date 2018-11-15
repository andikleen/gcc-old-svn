/* { dg-do compile } */
/* { dg-options "-fvartrace -fvartrace=locals -mptwrite -fdump-tree-vartrace" } */
/* { dg-final { scan-assembler-times "ptwrite" 4 } } */

int global;
int global2;

/* { dg-final { scan-tree-dump-times "(?n)asm_read.*insert.*ptwrite" 1 "vartrace" } } */
void asm_read(int i)
{
  asm("nop" :: "r" (i) : "memory");
}

/* { dg-final { scan-tree-dump-times "(?n)asm_write.*insert.*ptwrite" 1 "vartrace" } } */
void asm_write(void)
{
  asm("nop" : "=m" (global) :: "memory");
}

/* { dg-final { scan-tree-dump-times "(?n)asm_rw.*insert.*ptwrite" 2 "vartrace" } } */
void asm_rw(void)
{
  asm("nop" : "=m" (global) : "r" (global2) : "memory");
}
