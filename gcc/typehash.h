#if HOST_BITS_PER_WIDE_INT == 64
/* Spooky is the faster hash, but only on 64bit machines.  */
#include "spooky.h"
typedef Spooky type_incr_hash;
#else
/* Otherwise use the incremential murmur2a.  */
#include "murmurhash.h"
typdef cmurmurhash2A type_incr_hash;
#endif
