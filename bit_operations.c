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

// This creates the lookup table used in [1] to compute the number of trailing
// zeros.
//
// [1]: Leiserson, Charles E.; Prokop, Harald; Randall, Keith H. (1998),
//      Using de Bruijn Sequences to Index a 1 in a Computer Word
//      http://supertech.csail.mit.edu/papers/debruijn.pdf
//
// NOTE: res must be of size 32.
void init_bit_pos_lookup (int *res)
{
    int i = 0;
    uint32_t mask = 1;
    while (i< 32) {
        uint32_t idx = bit_mask_perfect_hash (mask);
        res[idx] = i;

        i++;
        mask <<= 1;
    }
}
int* create_bit_pos_lookup (mem_pool_t *pool)
{
    int *res = mem_pool_push_array (pool, 32, int);
    init_bit_pos_lookup (res);
    return res;
}

static inline
int bit_pos (uint32_t bit_mask)
{
    assert (single_bit_set(bit_mask));
    static int *bit_lookup = NULL;
    static int buff[32];
    if (!bit_lookup) {
        init_bit_pos_lookup (buff);
        bit_lookup = buff;
    }

    uint32_t idx = bit_mask_perfect_hash (bit_mask);
    return bit_lookup[idx];
}
