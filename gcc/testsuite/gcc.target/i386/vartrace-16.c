/* { dg-do compile } */
/* { dg-options "" } */
/* { dg-final { scan-assembler "ptwrite" } } */

struct {
  int __attribute__((vartrace)) x;
} v;

__attribute__((target("ptwrite"))) int f(void)
{
  return v.x;
}
