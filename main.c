#include <byteswap.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <unistd.h>

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

struct thread_ctx {
	void *arena;
	size_t id;
	size_t nprocs;
};

static unsigned char prefix[ADDRESS_BYTES];
static bool done = false;
static uint64_t reported_salt;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mux = PTHREAD_MUTEX_INITIALIZER;

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

#define HASH_TO_ADDR(X) ((unsigned char *)((X) + 12))

static inline void
print_salt(uint64_t salt)
{
	bool first = true;
	const unsigned char salt_data[8];

	*((uint64_t *)salt_data) = bswap_64(salt);
	printf("0x");
	for (size_t i = 0; i < 8; i++) {
		if (first && salt_data[i] == 0)
			continue;
		first = false;
		printf("%02x", salt_data[i]);
	}
}

static inline bool
iteration(unsigned char *state, const unsigned char *prefix, unsigned char *phase1, unsigned char *phase2, uint64_t salt)
{
	unsigned char output[SALT_BYTES * 8];

	KeccakP1600times8_InitializeAll(state);

	for (size_t i = 1; i < 8; i++) {
		*((uint64_t *)(phase1 + (i * 136) + ADDRESS_BYTES + 24)) = bswap_64(salt + i);
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

		addr = HASH_TO_ADDR(output + 32*j);

		if (__builtin_expect((prefix_cmp(prefix, addr) == 0), 0)) {
			salt += j;
			printf("Prefix matched\n");
			printf("Address: 0x");
			for (size_t i = 0; i < ADDRESS_BYTES; i++)
				printf("%02x", addr[i]);
			printf("\n");
			printf("Salt: ");
			print_salt(salt);
			printf("\n");
			done = true;
			pthread_cond_signal(&cond);
			return true;
		}
	}

	return false;
}

void *
thread_main(void *arg)
{
	struct thread_ctx *ctx = arg;
	uint64_t salt = ctx->id * 8;
	unsigned char *mem = malloc(KeccakP1600times8_statesAlignment + KeccakP1600times8_statesSizeInBytes);
	unsigned char *state = mem;
	unsigned char *phase1 = ctx->arena;
	unsigned char *phase2 = phase1 + 136 * 8;

	while ((uintptr_t)state % KeccakP1600times8_statesAlignment != 0)
		state += 1;

	while (done == false && iteration(state, prefix, phase1, phase2, salt) == false) {
		if (ctx->id == 0)
			reported_salt = salt;
		salt += 8 * ctx->nprocs;
	}

	free(mem);
	free(ctx->arena);
	free(ctx);
	return NULL;
}

unsigned char *
create_arena(void)
{
	unsigned char *out = calloc(136 * 8 * 2, sizeof(unsigned char));
	unsigned char node_address[ADDRESS_BYTES];
	unsigned char minipool_manager_address[ADDRESS_BYTES];
	unsigned char init_hash[INIT_HASH_BYTES];
	unsigned char ff = 0xff;

	unsigned char *phase1 = out;
	unsigned char *phase2 = phase1 + 136 * 8;

	parse_hex(node_address, NODE_ADDRESS, strlen(NODE_ADDRESS));
	parse_hex(minipool_manager_address, MINIPOOL_MANAGER_ADDRESS,
	    strlen(MINIPOOL_MANAGER_ADDRESS));
	parse_hex(init_hash, INIT_HASH, strlen(INIT_HASH));

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

	return out;
}

int
main(void)
{
	size_t nprocs = get_nprocs();
	uint64_t last_reported_salt = 0;
	time_t start = time(NULL);
	struct timespec ts = {};

	printf("Using %lu threads\n", nprocs);
	pthread_t *threads = calloc(nprocs, sizeof(pthread_t));

	parsePrefix(prefix, PREFIX);

	for (size_t i = 0; i < nprocs; i++) {
		struct thread_ctx *ctx = calloc(1, sizeof(struct thread_ctx));

		ctx->id = i;
		ctx->arena = create_arena();
		ctx->nprocs = nprocs;

		(void)pthread_create(&threads[i], NULL, thread_main, ctx);
	}

	pthread_mutex_lock(&mux);
	for (;;) {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 5;
		time_t iter = time(NULL);
		int ret = pthread_cond_timedwait(&cond, &mux, &ts);
		if (ret == ETIMEDOUT && !done) {
			time_t end = time(NULL);
			uint64_t diff = reported_salt - last_reported_salt;
			float rate = diff / (end - iter);
			float elapsed = end - start;
			const unsigned char salt_data[8];

			last_reported_salt += diff;

			printf("At salt ");
			print_salt(last_reported_salt);
			printf("... %0.2fs (%0.2fM salts/sec)\n", elapsed, rate / 1000000);
		} else if (done) {
			pthread_mutex_unlock(&mux);
			break;
		}
	}

	for (size_t i = 0; i < nprocs; i++) {
		pthread_join(threads[i], NULL);
	}

	free(threads);
	return 0;
}
