// Spooky Hash
// A 128-bit noncryptographic hash, for checksums and table lookup
// By Bob Jenkins.  Public domain.
//   Oct 31 2010: published framework, disclaimer ShortHash isn't right
//   Nov 7 2010: disabled ShortHash
//   Oct 31 2011: replace End, ShortMix, ShortEnd, enable ShortHash again
// Minor changes for gcc

#include "spooky.h"

#define ALLOW_UNALIGNED_READS 1

//
// short hash ... it could be used on any message,
// but it's used by Spooky just for short messages.
//
void Spooky::ShortHash (
  const void *message,
  size_t length,
  uint64_t *hash1,
  uint64_t *hash2)
{
  uint64_t buf[sc_numVars];
  union
  {
    const unsigned char *p8;
    uint32_t *p32;
    uint64_t *p64;
    size_t i;
  } u;

  u.p8 = (const unsigned char *)message;

  if (!ALLOW_UNALIGNED_READS && (u.i & 0x7))
    {
      memcpy (buf, message, length);
      u.p64 = buf;
    }

  size_t remainder = length%32;
  uint64_t a=*hash1;
  uint64_t b=*hash2;
  uint64_t c=0;
  uint64_t d=0;

  if (length > 15)
    {
      const uint64_t *end = u.p64 + (length/32)*4;

      // handle all complete sets of 32 bytes
      for (; u.p64 < end; u.p64 += 4)
        {
          c += u.p64[0];
          d += u.p64[1];
          ShortMix (a,b,c,d);
          a += u.p64[2];
          b += u.p64[3];
        }

      //Handle the case of 16+ remaining bytes.
      if (remainder >= 16)
        {
          c += u.p64[0];
          d += u.p64[1];
          ShortMix (a,b,c,d);
          u.p64 += 2;
          remainder -= 16;
        }
    }

  // Handle the last 0..15 bytes, and its length
  d += ((uint64_t)length) << 56;
  switch (remainder)
    {
    case 15:
      d += ((uint64_t)u.p8[14]) << 48;
    case 14:
      d += ((uint64_t)u.p8[13]) << 40;
    case 13:
      d += ((uint64_t)u.p8[12]) << 32;
    case 12:
      d += u.p32[2];
      c += u.p64[0];
      break;
    case 11:
      d += ((uint64_t)u.p8[10]) << 16;
    case 10:
      d += ((uint64_t)u.p8[9]) << 8;
    case 9:
      d += (uint64_t)u.p8[8];
    case 8:
      c += u.p64[0];
      break;
    case 7:
      c += ((uint64_t)u.p8[6]) << 48;
    case 6:
      c += ((uint64_t)u.p8[5]) << 40;
    case 5:
      c += ((uint64_t)u.p8[4]) << 32;
    case 4:
      c += u.p32[0];
      break;
    case 3:
      c += ((uint64_t)u.p8[2]) << 16;
    case 2:
      c += ((uint64_t)u.p8[1]) << 8;
    case 1:
      c += (uint64_t)u.p8[0];
      break;
    case 0:
      c += sc_const;
      d += sc_const;
    }
  ShortEnd (a,b,c,d);
  *hash1 = a;
  *hash2 = b;
}




// do the whole hash in one call
void Spooky::Hash128(
  const void *message,
  size_t length,
  uint64_t *hash1,
  uint64_t *hash2)
{
  if (length < sc_blockSize)
    {
      ShortHash (message, length, hash1, hash2);
      return;
    }

  uint64_t h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11;
  uint64_t buf[sc_numVars];
  uint64_t *end;
  union
  {
    const unsigned char *p8;
    uint64_t *p64;
    size_t i;
  } u;
  size_t remainder;

  h0 = h3 = h6 = h9  = *hash1;
  h1 = h4 = h7 = h10 = *hash2;
  h2 = h5 = h8 = h11 = sc_const;

  u.p8 = (const unsigned char *)message;
  end = u.p64 + (length/sc_blockSize)*sc_numVars;

  // handle all whole blocks of sc_blockSize bytes
  if (ALLOW_UNALIGNED_READS || (u.i & 0x7) == 0)
    {
      for (; u.p64 < end; u.p64+=sc_numVars)
        {
          Mix (u.p64, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
        }
    }
  else
    {
      for (; u.p64 < end; u.p64 += sc_numVars)
        {
          memcpy (buf, u.p64, sc_blockSize);
          Mix (buf, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
        }
    }

  // handle the last partial block of sc_blockSize bytes
  remainder = (length - ((const unsigned char *)end-(const unsigned char *)message));
  memcpy (buf, end, remainder);
  memset (((unsigned char *)buf)+remainder, 0, sc_blockSize-remainder);
  ((unsigned char *)buf)[sc_blockSize-1] = (unsigned char)length;
  Mix (buf, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);

  // do some final mixing
  End (h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
  *hash1 = h0;
  *hash2 = h1;
}



// init spooky state
void Spooky::Init(uint64_t seed1, uint64_t seed2)
{
  m_length = 0;
  m_remainder = 0;
  m_state[0] = seed1;
  m_state[1] = seed2;
}


// add a message fragment to the state
void Spooky::Update(const void *message, size_t length)
{
  uint64_t h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11;
  size_t newLength = length + m_remainder;
  unsigned char  remainder;
  union
  {
    const unsigned char *p8;
    uint64_t *p64;
    size_t i;
  } u;
  const uint64_t *end;

  // Is this message fragment too short?  If it is, stuff it away.
  if (newLength < sc_blockSize)
    {
      memcpy (&((unsigned char *)m_data)[m_remainder], message, length);
      m_length = length + m_length;
      m_remainder = (unsigned char)newLength;
      return;
    }

  // init the variables
  if (m_length < sc_blockSize)
    {
      h0 = h3 = h6 = h9  = m_state[0];
      h1 = h4 = h7 = h10 = m_state[1];
      h2 = h5 = h8 = h11 = sc_const;
    }
  else
    {
      h0 = m_state[0];
      h1 = m_state[1];
      h2 = m_state[2];
      h3 = m_state[3];
      h4 = m_state[4];
      h5 = m_state[5];
      h6 = m_state[6];
      h7 = m_state[7];
      h8 = m_state[8];
      h9 = m_state[9];
      h10 = m_state[10];
      h11 = m_state[11];
    }
  m_length = length + m_length;

  // if we've got anything stuffed away, use it now
  if (m_remainder)
    {
      unsigned char prefix = sc_blockSize-m_remainder;
      memcpy (&((unsigned char *)m_data)[m_remainder],
             message,
             prefix);
      u.p64 = m_data;
      Mix (u.p64, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
      u.p8 = ((const unsigned char *)message) + prefix;
      length -= prefix;
    }
  else
    {
      u.p8 = (const unsigned char *)message;
    }

  // handle all whole blocks of sc_blockSize bytes
  end = u.p64 + (length/sc_blockSize)*sc_numVars;
  remainder = (unsigned char)(length-((const unsigned char *)end-u.p8));
  if (ALLOW_UNALIGNED_READS || (u.i & 0x7) == 0)
    {
      for (; u.p64 < end; u.p64 += sc_numVars)
        {
          Mix (u.p64, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
        }
    }
  else
    {
      for (; u.p64 < end; u.p64 += sc_numVars)
        {
          memcpy (m_data, u.p8, sc_blockSize);
          Mix (m_data, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
        }
    }

  // stuff away the last few bytes
  m_remainder = remainder;
  memcpy (m_data, end, remainder);

  // stuff away the variables
  m_state[0] = h0;
  m_state[1] = h1;
  m_state[2] = h2;
  m_state[3] = h3;
  m_state[4] = h4;
  m_state[5] = h5;
  m_state[6] = h6;
  m_state[7] = h7;
  m_state[8] = h8;
  m_state[9] = h9;
  m_state[10] = h10;
  m_state[11] = h11;
}


// report the hash for the concatenation of all message fragments so far
void Spooky::Final(uint64_t *hash1, uint64_t *hash2)
{
  uint64_t h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11;
  const uint64_t *data;
  unsigned char remainder, prefix;

  // init the variables
  if (m_length < sc_blockSize)
    {
      ShortHash ( m_data, m_length, hash1, hash2);
      return;
    }

  h0 = m_state[0];
  h1 = m_state[1];
  h2 = m_state[2];
  h3 = m_state[3];
  h4 = m_state[4];
  h5 = m_state[5];
  h6 = m_state[6];
  h7 = m_state[7];
  h8 = m_state[8];
  h9 = m_state[9];
  h10 = m_state[10];
  h11 = m_state[11];

  // mix in the last partial block, and the length mod sc_blockSize
  remainder = m_remainder;
  prefix = (unsigned char)(sc_blockSize-remainder);
  memset (&((unsigned char *)m_data)[remainder], 0, prefix);
  ((unsigned char *)m_data)[sc_blockSize-1] = (unsigned char)m_length;
  data = (const uint64_t *)m_data;
  Mix (data, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);

  // do some final mixing
  End (h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
  *hash1 = h0;
  *hash2 = h1;
}
