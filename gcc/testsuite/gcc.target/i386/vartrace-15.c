/* { dg-do compile } */
/* { dg-options "-mptwrite -fvartrace" } */
/* { dg-final { scan-assembler-not "ptwrite" } } */

struct {
  int __attribute__((vartrace)) x;
} v;

__attribute__((target("no-ptwrite"))) int f(void)
{
  return v.x;
}
