#include <byteswap.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "KeccakP-1600-times8-SnP.h"

// ----------- CONFIG VARIABLES -----------
// This is the prefix you're searching for
#define PREFIX "0x000001"
// This is your node wallet address
#define NODE_ADDRESS "0x152CC1dEb3f343384a5064Aa322c4CFf6b3fFAe8"
// You MUST generate this using smartnode!
#define INIT_HASH "0xddff5ce23a92998f2b0b270eda7afd877aa25c3df35a384f723656881fab1964"
// This is the mainnet minipool manager contract address
#define MINIPOOL_MANAGER_ADDRESS "0x6293b8abc1f36afb22406be5f96d893072a8cf3a"
// -------------- END CONFIG --------------

#define ADDRESS_BYTES 20
#define SALT_BYTES 32
#define INIT_HASH_BYTES 32
#define LANE_SIZE 8
// In half-bytes
#define PREFIX_LENGTH (strlen(PREFIX) - 2)

_Static_assert(sizeof(PREFIX) <= sizeof(NODE_ADDRESS), "Prefix must be at most 20 chars");
_Static_assert(sizeof(NODE_ADDRESS) == 43, "Invalid node address");
_Static_assert(sizeof(MINIPOOL_MANAGER_ADDRESS) == 43, "Invalid minipool manager address");

static unsigned char
hex_to_char(char in)
{
	in = tolower(in);
	if (in >= '0' && in <= '9') {
		return (char)(in - '0');
	}

	return (char)(in - 'a' + 10);
}

static void
parse_hex(unsigned char *dst, const char *in, size_t nstr)
{

	for (size_t i = 2; i < nstr - 1; i += 2) {
		unsigned char hi = hex_to_char(in[i]);
		unsigned char lo = hex_to_char(in[i + 1]);
		unsigned char binary = (hi << 4) | lo;

		dst[i/2 - 1] = binary;
	}

	return;
}

static void
parsePrefix(unsigned char *dst, const char *in)
{
	size_t len;
	size_t max;
	char *write_head = dst;

	memset(dst, 0, ADDRESS_BYTES);

	in = in + 2;
	len = strlen(in);
	max = len % 2 == 0 ? len : len - 1;
	for (size_t i = 0; i < max; i += 2) {
		unsigned char hi = hex_to_char(in[i]);
		unsigned char lo = hex_to_char(in[i + 1]);
		unsigned char binary = (hi << 4) | lo;

		*write_head = binary;
		write_head++;
	}

	if (len % 2 == 0)
		return;

	// If the prefix is an odd length, we need to copy the last half byte by hand.
	*write_head = hex_to_char(in[max]) << 4;
	return;

}

static inline int
prefix_cmp(const unsigned char *a, const unsigned char *b)
{
	char hi_a;
	char hi_b;

	if (PREFIX_LENGTH % 2 == 0)
		return memcmp(a, b, PREFIX_LENGTH / 2);
	if (memcmp(a, b, (PREFIX_LENGTH - 1) / 2) != 0)
		return 1;
	hi_a = a[(PREFIX_LENGTH - 1)/2] & 0xf0;
	hi_b = b[(PREFIX_LENGTH - 1)/2] & 0xf0;
	return (hi_a ^ hi_b) == 0x00 ? 0 : 1;
}

static inline unsigned char *
hash_to_address(unsigned char result[static SALT_BYTES])
{

	return result + 12;
}

static inline void
next_salt(unsigned char salt[static SALT_BYTES], uint32_t incr)
{
	uint64_t hi = bswap_64(*(uint64_t *)(salt + 24));
	uint64_t mhi = bswap_64(*(uint64_t *)(salt + 16));
	uint64_t mlo = bswap_64(*(uint64_t *)(salt + 8));
	uint64_t lo = bswap_64(*(uint64_t *)(salt));
	uint8_t carry = 0;

	/* hi is the least significant word, since it was highest in memory */
	if (UINT64_MAX - incr >= hi) {
		hi += incr;
		goto store;
	}

	hi += incr;
	incr = 1;

	if (UINT64_MAX - incr >= mhi) {
		mhi += incr;
		goto store;
	}

	mhi += incr;

	if (UINT64_MAX - incr >= mlo) {
		mlo += incr;
		goto store;
	}

	mlo += incr;

	if (UINT64_MAX - incr >= lo) {
		lo += incr;
		goto store;
	}

store:
	*((uint64_t *)salt) = bswap_64(lo);
	*((uint64_t *)(salt + 8)) = bswap_64(mlo);
	*((uint64_t *)(salt + 16)) = bswap_64(mhi);
	*((uint64_t *)(salt + 24)) = bswap_64(hi);
}

bool
iteration(unsigned char *state, const unsigned char *prefix, unsigned char *phase1, unsigned char *phase2, unsigned char *salt)
{
	unsigned char output[SALT_BYTES * 8];

	KeccakP1600times8_InitializeAll(state);

	memcpy(phase1 + ADDRESS_BYTES, salt, SALT_BYTES);
	for (size_t i = 1; i < 8; i++) {
		memcpy(phase1 + (i * 136) + ADDRESS_BYTES, phase1 + ((i-1) * 136) + ADDRESS_BYTES, SALT_BYTES);
		next_salt(phase1 + (i * 136) + ADDRESS_BYTES, 1);
	}
	KeccakP1600times8_AddLanesAll(state, phase1, 136 / LANE_SIZE, 136 / LANE_SIZE);
	KeccakP1600times8_PermuteAll_24rounds(state);
	KeccakP1600times8_ExtractLanesAll(state, output, 4, 4);

	for (size_t i = 0; i < 8; i++) {
		memcpy(phase2 + (i * 136) + 1 + ADDRESS_BYTES, output + i*32, SALT_BYTES);
	}
	KeccakP1600times8_InitializeAll(state);
	KeccakP1600times8_AddLanesAll(state, phase2, 136 / LANE_SIZE, 136 / LANE_SIZE);
	KeccakP1600times8_PermuteAll_24rounds(state);
	KeccakP1600times8_ExtractLanesAll(state, output, 4, 4);

	for (size_t j = 0; j < 8; j++) {
		const unsigned char *addr;

		addr = hash_to_address(output + 32*j);

		if (__builtin_expect((prefix_cmp(prefix, addr) == 0), 0)) {
			next_salt(salt, j);
			printf("Prefix matched\n");
			printf("Address: 0x");
			for (size_t i = 0; i < ADDRESS_BYTES; i++)
				printf("%02x", addr[i]);
			printf("\n");
			printf("Salt: 0x");
			for (size_t i = 0; i < SALT_BYTES; i++) {
				if (i != SALT_BYTES - 1 && salt[i] == 0)
					continue;
				printf("%02x", salt[i]);
			}
			printf("\n");
			return true;
		}
	}

	return false;
}

int
thread_main(void)
{
	unsigned char salt[SALT_BYTES];
	unsigned char node_address[ADDRESS_BYTES];
	unsigned char minipool_manager_address[ADDRESS_BYTES];
	unsigned char init_hash[INIT_HASH_BYTES];
	unsigned char prefix[ADDRESS_BYTES];
	unsigned char ff = 0xff;
	unsigned char *mem = malloc(KeccakP1600times8_statesAlignment + KeccakP1600times8_statesSizeInBytes);
	unsigned char *state = mem;
	while ((uintptr_t)state % KeccakP1600times8_statesAlignment != 0)
		state += 2;

	unsigned char phase1[136 * 8];
	unsigned char phase2[136 * 8];

	memset(salt, 0, SALT_BYTES);
	memset(phase1, 0, sizeof(phase1));
	memset(phase2, 0, sizeof(phase2));

	parse_hex(node_address, NODE_ADDRESS, strlen(NODE_ADDRESS));
	parse_hex(minipool_manager_address, MINIPOOL_MANAGER_ADDRESS,
	    strlen(MINIPOOL_MANAGER_ADDRESS));
	parse_hex(init_hash, INIT_HASH, strlen(INIT_HASH));
	parsePrefix(prefix, PREFIX);

	/* All ranges left-inclusive only */
	for (size_t i = 0; i < 8; i++) {
		/* Bytes 0-20 are address */
		memcpy(phase1 + i*136, node_address, ADDRESS_BYTES);
		/* 32 byte hole at 20-52 */
		/* 1 byte for padding at byte 52 */
		phase1[i*136 + ADDRESS_BYTES + SALT_BYTES] = 0x01;
		/* End padding at byte 136 */
		phase1[i*136 + 135] = 0x80;
	}

	for (size_t i = 0; i < 8; i++) {
		/* First byte is 0xff */
		memcpy(phase2 + i*136, &ff, 1);
		/* Bytes 1-21 are the minipool mgr address */
		memcpy(phase2 + i*136 + 1, minipool_manager_address, ADDRESS_BYTES);
		/* 32 byte hole at bytes 21-53 */
		/* Bytes 53-85 are init_hash */
		memcpy(phase2 + i*136 + 1 + ADDRESS_BYTES + SALT_BYTES, init_hash, INIT_HASH_BYTES);
		/* Byte 85 is padding */
		phase2[i*136 + 1 + ADDRESS_BYTES + SALT_BYTES + INIT_HASH_BYTES] = 0x01;
		/* End padding at byte 136 */
		phase2[i*136 + 135] = 0x80;
	}

	for (;;) {
		if (iteration(state, prefix, phase1, phase2, salt))
			return 0;
		next_salt(salt, 8);
	}

	return 0;
}

int
main(void)
{

	return thread_main();
}
