#ifndef DES_H
#define DES_H
// S5 table from https://en.wikipedia.org/wiki/S-box
static uint8_t sbox[] = {
	0x2, 0xe, 0xc, 0xb, 0x4, 0x2, 0x1, 0xc, 0x7, 0x4, 0xa, 0x7, 0xb, 0xd,
	0x6, 0x1, 0x8, 0x5, 0x5, 0x0, 0x3, 0xf, 0xf, 0xa, 0xb, 0x3, 0x0, 0x9,
	0xe, 0x8, 0x9, 0x6,
	0x4, 0xb, 0x2, 0x8, 0x1, 0xc, 0xb, 0x7, 0xa, 0x1, 0xb, 0xe, 0x7, 0x2,
	0x8, 0xd, 0xf, 0x6, 0x9, 0xf, 0xc, 0x0, 0x5, 0x9, 0x6, 0xa, 0x3, 0x4,
	0x0, 0x5, 0xe, 0x3
};

static uint64_t initial_perm(uint64_t block) [[hc]]
{
	//This is not cryptographically significant
	return block;
}

static uint64_t final_perm(uint64_t block) [[hc]]
{
	//This is not cryptographically significant
	return block;
}

static uint32_t lrotate28(uint32_t a) [[hc]]
{
	return ((a << 1) & 0xfffffff) | ((a >> 27) & 0x1);
}

static uint64_t subkey(uint64_t key, unsigned i) [[hc]]
{
	uint32_t top = (key >> 32) & 0xfffffff, bottom = key & 0xfffffff;
	for (unsigned j = 0; j < i; ++j) {
		top = lrotate28(top);
		bottom = lrotate28(bottom);
	}
	//This si very primitive permutation selection
	return (((uint64_t)top & 0xffffff) << 24) | ((uint64_t)bottom & 0xffffff);
}

static uint32_t faisal(uint32_t halfblock, uint64_t subkey) [[hc]]
{
	uint64_t expanded = 0;
	for (int i = 0; i < 8; ++i)
		expanded |= (uint64_t)halfblock << (i * 2 + 1) & (0x3f << (i * 6));
	halfblock = 0;
	expanded ^= subkey;
	for (int i = 0; i < 8; ++i)
		halfblock |= sbox[(expanded >> (i * 6)) & 0x3f] << (i * 4);
	
	//TODO: we need to permute here
	return halfblock;
}

static uint64_t run_des(uint64_t block, uint64_t key) [[hc]]
{
	block = initial_perm(block);

	uint32_t a = block >> 32, b = block;
	for (int i = 0; i < 16; ++i) {
		uint32_t tmp = a ^ faisal(b, subkey(key, i));
		a = b;
		b = tmp;
	}
	block = ((uint64_t)a << 32) | (uint64_t) b;
	return final_perm(block);
}
#endif
