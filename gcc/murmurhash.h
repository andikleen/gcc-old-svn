/* CMurmurHash2A, by Austin Appleby with minor changes for gcc.
   This is a variant of MurmurHash2 modified to use the Merkle-Damgard
   construction. Bulk speed should be identical to Murmur2, small-key speed
   will be 10%-20% slower due to the added overhead at the end of the hash.
   This is a sample implementation of MurmurHash2A designed to work
   incrementally.

   Only uses 4 bytes, no special 64bit implementation.  */

#ifndef CMURMURHASH_H
#define CMURMURHASH_H 1

#define mmix(h,k) { k *= m; k ^= k >> r; k *= m; h *= m; h ^= k; }

class cmurmurhash2A
{
public:

  void begin(uint32_t seed = 0)
  {
    m_hash  = seed;
    m_tail  = 0;
    m_count = 0;
    m_size  = 0;
  }

  /* Note the caller must pass in 4 byte aligned values */
  void add(const unsigned char * data, int len)
  {
    m_size += len;

    mix_tail (data,len);

    while(len >= 4)
      {
        uint32_t k = *(const uint32_t *)data;

        mmix (m_hash, k);

        data += 4;
        len -= 4;
      }

    mix_tail (data, len);
  }

  void add(const void * data, int len) { add ((const unsigned char *) data, len); }

  void add_int(uint32_t i)
  {
    add ((const unsigned char *)&i, 4);
  }

  void add_wide_int(uint64_t i)
  {
    add ((const unsigned char *)&i, 8);
  }

  void add_ptr(void *p)
  {
    add ((const unsigned char *)&p, sizeof (void *));
  }

  void merge(cmurmurhash2A &s)
  {
    // better way?
    add_int (s.end());
  }

  /* Does not destroy state. */
  uint32_t end(void)
  {
    uint32_t t_hash = m_hash;
    uint32_t t_tail = m_tail;
    uint32_t t_size = m_size;

    mmix (t_hash, t_tail);
    mmix (t_hash, t_size);

    t_hash ^= t_hash >> 13;
    t_hash *= m;
    t_hash ^= t_hash >> 15;

    return t_hash;
  }

private:

  static const uint32_t m = 0x5bd1e995;
  static const int r = 24;

  void mix_tail (const unsigned char * & data, int & len)
  {
    while (len && ((len<4) || m_count))
      {
        m_tail |= (*data++) << (m_count * 8);

        m_count++;
        len--;

        if(m_count == 4)
          {
            mmix (m_hash, m_tail);
            m_tail = 0;
            m_count = 0;
          }
      }
  }

  uint32_t m_hash;
  uint32_t m_tail;
  uint32_t m_count;
  uint32_t m_size;
};

#endif
