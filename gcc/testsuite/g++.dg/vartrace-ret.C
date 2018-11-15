/* { dg-do compile { target i?86-*-* x86_64-*-* } } */
/* { dg-options "-O2 -mptwrite -fvartrace=returns " } */
/* { dg-final { scan-assembler-not "ptwrite" } } */

class foo { 
public:
    short a;
    short b;
};

foo f1()
{
    foo x = { 1, 2 };
    return x;
}


