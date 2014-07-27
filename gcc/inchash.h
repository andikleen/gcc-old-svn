/* An incremental hash abstract data type.
   Copyright (C) 2014 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef INCHASH_H
#define INCHASH_H 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "hashtab.h"

/* This file implements an incremential hash function ADT, to be used
   by code that incrementially hashes a lot of unrelated data
   (not in a single memory block) into a single value. The goal
   is to make it easy to plug in efficient hash algorithms. */

extern hashval_t iterative_hash_host_wide_int (HOST_WIDE_INT, hashval_t);
extern hashval_t iterative_hash_hashval_t (hashval_t, hashval_t);

namespace inchash
{

/* inchash using plain old jhash from gcc's tree.c.  */

class hash_jhash
{
 public:

  /* Start incremential hashing, optionally with SEED.  */
  hash_jhash (hashval_t seed = 0)
  {
    val = seed;
  }

  /* End incremential hashing and provide the final value.  */
  hashval_t end ()
  {
    return val;
  }

  /* Add unsigned value V.  */
  void add_int (unsigned v)
  {
    val = iterative_hash_hashval_t (v, val);
  }

  /* Add HOST_WIDE_INT value V.  */
  void add_wide_int (HOST_WIDE_INT v)
  {
    val = iterative_hash_host_wide_int (v, val);
  }

   /* Add a memory block DATA with size LEN.  */
  void add (const void *data, size_t len)
  {
    val = iterative_hash (data, len, val);
  }

  /* Merge hash value OTHER.  */
  void merge_hash (hashval_t other)
  {
    val = iterative_hash_hashval_t (other, val);
  }

  /* Hash in state from other inchash OTHER.  */
  void merge (hash_jhash &other)
  {
    merge_hash (other.val);
  }

  template<class T> void add_object(T &obj)
  {
    add (&obj, sizeof(T));
  }

  /* Support for commutative hashing. Add A and B in a defined order
     based on their value. This is useful for hashing commutative
     expressions, so that A+B and B+A get the same hash.  */

  void add_commutative (hash_jhash &a, hash_jhash &b)
  {
    if (a.end() > b.end())
      {
        merge (b);
	merge (a);
      }
    else
      {
        merge (a);
        merge (b);
      }
  }

 private:
  hashval_t val;
};

/* Use spooky hash on a 64bit host. This avoids doing all work on
   every update.  */

#include "spooky.h"

class hash_spooky
{
 public:
  /* Start incremential hashing, optionally with SEED.  */
  hash_spooky  (hashval_t seed = 0)
  {
   sp.Init (seed, seed);
  }

  /* End incremential hashing and provide the final value.  */
  hashval_t end ()
  {
    uint64_t a, b;
    sp.Final (&a, &b);
    return (hashval_t) a;
  }

  /* Add unsigned value V.  */
  void add_int (unsigned v)
  {
    sp.Update (&v, sizeof (v));
  }

  /* Add HOST_WIDE_INT value V.  */
  void add_wide_int (HOST_WIDE_INT v)
  {
    sp.Update (&v, sizeof (v));
  }

   /* Add a memory block DATA with size LEN.  */
  void add (const void *data, size_t len)
  {
    sp.Update (data, len);
  }

  /* Merge hash value OTHER.  */
  void merge_hash (hashval_t other)
  {
    sp.Update (&other, sizeof (hashval_t));
  }

  /* Hash in state from other inchash OTHER.  */
  void merge (hash_spooky &other)
  {
    uint64_t a, b;
    other.sp.Final (&a, &b);
    add_wide_int (a);
    add_wide_int (b);
  }

  /* Support for commutative hashing. Add A and B in a defined order
     based on their value. This is useful for hashing commutative
     expressions, so that A+B and B+A get the same hash.  */

  void add_commutative (hash_spooky &a, hash_spooky &b)
  {
    uint64_t a0, b0, a1, b1;

    a.sp.Final (&a0, &a1);
    b.sp.Final (&b0, &b1);

    if (a0 > b0 || (a0 == b0 && a1 > b1))
      {
	add_wide_int (b0);
	add_wide_int (b1);
	add_wide_int (a0);
	add_wide_int (a1);
      }
    else
      {
	add_wide_int (a0);
	add_wide_int (a1);
	add_wide_int (b0);
	add_wide_int (b1);
      }
  }

 private:
   Spooky sp;
};

#if BITS_PER_HOST_WIDE_INT == 64
#define hash_impl hash_spooky
#else
#define hash_impl hash_jhash
#endif

/* Common utility functions for the inchash classes.  */

class hash : public hash_impl
{
 public:
  hash(hashval_t seed = 0) : hash_impl(seed), bits(0) {}

  /* Hash in pointer PTR.  */
  void add_ptr (void *ptr)
  {
    add (&ptr, sizeof (ptr));
  }

  template<class T> void add_object(T &obj)
  {
    add(&obj, sizeof(T));
  }

  /* Support for accumulating boolean flags */

  /* Queue flag FLAG to the hash. Must call commit_flag later.  */
  void add_flag (bool flag)
  {
    bits = (bits << 1) | flag;
  }

  /* Commit multiple flags to the hash.  */
  void commit_flag ()
  {
    add_int (bits);
    bits = 0;
  }

 private:
  unsigned bits;
};

}

#endif
