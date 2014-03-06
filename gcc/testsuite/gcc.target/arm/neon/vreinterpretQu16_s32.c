/* Test the `vreinterpretQu16_s32' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0" } */
/* { dg-add-options arm_neon } */

#include "arm_neon.h"

void test_vreinterpretQu16_s32 (void)
{
  uint16x8_t out_uint16x8_t;
  int32x4_t arg0_int32x4_t;

  out_uint16x8_t = vreinterpretq_u16_s32 (arg0_int32x4_t);
}

/* { dg-final { cleanup-saved-temps } } */
