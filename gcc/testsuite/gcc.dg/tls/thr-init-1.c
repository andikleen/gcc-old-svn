/* { dg-require-effective-target tls } */
/* { dg-do compile } */

static __thread int fstat ;
static __thread int fstat = 1 ;
static __thread int fstat ;
static __thread int fstat = 2; /* { dg-error "redefinition of 'fstat'" } */
				/* { dg-message "note: previous definition of 'fstat' was here" "" { target *-*-* } 5 } */
