/* { dg-do compile } */
/* { dg-options "-O2 -mptwrite " } */
/* { dg-final { scan-assembler "ptwrite.*r" } } */
/* { dg-final { scan-assembler "ptwrite.*e" } } */

#include <x86intrin.h>

void ptwrite1(void)
{
  _ptwrite_u32 (1);
  _ptwrite_u64 (2);
}
