/*
 * Copiright (C) 2019 Santiago León O.
 */

static inline
bool single_bit_set (uint32_t mask)
{
    return mask && !(mask & (mask-1));
}

// This function maps each 32 bit integers with a single bit set to a unique
// number in the range [0..31]. To do this it uses a de Brujin sequence as
// described in the algorithm from Leiserson, Prokop, and Randal to compute the
// number of trailing zeros [1].
//
// [1]: Leiserson, Charles E.; Prokop, Harald; Randall, Keith H. (1998),
//      Using de Bruijn Sequences to Index a 1 in a Computer Word
//      http://supertech.csail.mit.edu/papers/debruijn.pdf
//
// TODO: If available, a CTZ intrinsic should be faster. Look into that.
static inline
uint32_t bit_mask_perfect_hash (uint32_t mask)
{
    assert (single_bit_set (mask));
    return (uint32_t)(mask*0x077CB531UL) >> 27;
}

