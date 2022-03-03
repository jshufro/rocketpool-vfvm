#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "KeccakHash.h"

// ----------- CONFIG VARIABLES -----------
// This is the prefix you're searching for
#define PREFIX "0x000001"
// This is your node wallet address
#define NODE_ADDRESS "0xbb143cCca153bb8ACFBe02E5d61BCEbf5027C35F"
// You MUST generate this using smartnode!
#define INIT_HASH "0xa1ca25e40fe431d87aa7f9a8b1a74f71505c727c2a5a07a3ae0dfb9760730df0"
// This is the mainnet minipool manager contract address
#define MINIPOOL_MANAGER_ADDRESS "0x6293b8abc1f36afb22406be5f96d893072a8cf3a"
// -------------- END CONFIG --------------

#define ADDRESS_BYTES 20
#define SALT_BYTES 32
#define INIT_HASH_BYTES 32
// In half-bytes
#define PREFIX_LENGTH (strlen(PREFIX) - 2)

_Static_assert(sizeof(PREFIX) <= sizeof(NODE_ADDRESS), "Prefix must be at most 20 chars");
_Static_assert(sizeof(NODE_ADDRESS) == 43, "Invalid node address");
_Static_assert(sizeof(MINIPOOL_MANAGER_ADDRESS) == 43, "Invalid minipool manager address");

#define KECCAK_INIT(HASHER)						\
	do {								\
		if (Keccak_HashInitialize(HASHER,			\
		    1088, 1600-1088, 32 * 8, 0x01) != KECCAK_SUCCESS)	\
			return -1;					\
	} while (0)

#define KECCAK_UPDATE(HASHER, SRC, BYTES)				\
	do {								\
		if (Keccak_HashUpdate(&inst, SRC, 8 * BYTES)		\
		    != KECCAK_SUCCESS)					\
			return -1;					\
	} while (0)

#define KECCAK_FINAL(HASHER, DST)					\
	do {								\
		if (Keccak_HashFinal(HASHER, DST) !=			\
		    KECCAK_SUCCESS)					\
			return -1;					\
	} while (0)

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

static int
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

unsigned char *
hash_to_address(unsigned char result[static SALT_BYTES])
{

	return result + 12;
}

void
next_salt(unsigned char salt[static SALT_BYTES])
{
	uint8_t *frags = (uint8_t *)salt;

	for (size_t i = 32 - 1; i >= 0; i--) {
		if (frags[i] != UINT8_MAX) {
			frags[i]++;
			return;
		}

		frags[i] = 0;
	}
	exit(1);
	return;
}

int
main(void)
{
	unsigned char salt[SALT_BYTES];
	unsigned char output[SALT_BYTES];
	unsigned char node_address[ADDRESS_BYTES];
	unsigned char minipool_manager_address[ADDRESS_BYTES];
	unsigned char init_hash[INIT_HASH_BYTES];
	unsigned char prefix[ADDRESS_BYTES];
	unsigned char ff = 0xff;
	const unsigned char *addr;
	Keccak_HashInstance inst;
	HashReturn ret;

	memset(salt, 0, SALT_BYTES);

	parse_hex(node_address, NODE_ADDRESS, strlen(NODE_ADDRESS));
	parse_hex(minipool_manager_address, MINIPOOL_MANAGER_ADDRESS,
	    strlen(MINIPOOL_MANAGER_ADDRESS));
	parse_hex(init_hash, INIT_HASH, strlen(INIT_HASH));
	parsePrefix(prefix, PREFIX);

	for (;;) {
		KECCAK_INIT(&inst);

		KECCAK_UPDATE(&inst, node_address, ADDRESS_BYTES);
		KECCAK_UPDATE(&inst, salt, SALT_BYTES);
		KECCAK_FINAL(&inst, output);

		KECCAK_INIT(&inst);
		KECCAK_UPDATE(&inst, &ff, 1);
		KECCAK_UPDATE(&inst, minipool_manager_address, ADDRESS_BYTES);
		KECCAK_UPDATE(&inst, output, SALT_BYTES);
		KECCAK_UPDATE(&inst, init_hash, INIT_HASH_BYTES); 
		KECCAK_FINAL(&inst, output);

		addr = hash_to_address(output);

		next_salt(salt);
		if (prefix_cmp(prefix, addr) == 0) {
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
			return 0;
		}
	}

	return 0;
}
