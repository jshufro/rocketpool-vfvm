#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <byteswap.h>
#include <pthread.h>
#include <stdatomic.h>

#include "plugin.h"

#define PRINTF(FMT, ...) printf("(partial-output-plugin) " FMT, ##__VA_ARGS__)
#define ADDRESS_BYTES 20

/* Plugin Globals */
static atomic_size_t longest_prefix_len = ATOMIC_VAR_INIT(0);
static uint64_t longest_prefix_salt;
static unsigned char longest_prefix_addr[ADDRESS_BYTES];
static size_t target_prefix_len;
static unsigned char target_prefix_addr[ADDRESS_BYTES];
pthread_mutex_t match_mutex = PTHREAD_MUTEX_INITIALIZER;

// Copied from main
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

static void
on_interrupt()
{
	PRINTF("Received SIGINT signal (ctrl-c)\n");
	PRINTF("Longest partial prefix match has len %ld\n\tAddress: 0x", longest_prefix_len);
	for (size_t i = 0; i < ADDRESS_BYTES; i++)
		printf("%02x", longest_prefix_addr[i]);
	printf("\n\tSalt   : ");
	print_salt(longest_prefix_salt);
	printf("\n");
}

/* Comparison function */
static inline size_t
get_prefix_len(const unsigned char *a)
{
	size_t p = 0;
	size_t len = 0;

	// optimization: check if the longest + 1 nibble matches, if not we can immediately stop
	unsigned char cmp;
	const size_t lpl = atomic_load(&longest_prefix_len);

	if (lpl & 1) { // uneven -> next one should be in last nibble
		cmp = 0x0F;
	} else {
		cmp = 0xF0;
	}
	if ((a[(lpl>>1)] & cmp) != (target_prefix_addr[(lpl>>1)] & cmp)) {
		return lpl;
	}

	// The rest is the normal left to right check
	while (p < ADDRESS_BYTES) {
		if ((a[p] & 0xF0) == (target_prefix_addr[p] & 0xF0))
			len++;
		else
			break;
		if ((a[p] & 0x0F) == (target_prefix_addr[p] & 0x0F))
			len++;
		else
			break;
		p++;
	}
	return len;
}

/*
 * Called before mining begins.
 * Initialize state here- once mining begins, calls to on_iteration will come
 * from the worker threads, and must be thread-safe.
 */
static bool
on_init(const unsigned char * const prefix, uint8_t prefix_len)
{
	longest_prefix_salt = 0;
	target_prefix_len = prefix_len;
	memset(target_prefix_addr, 0, ADDRESS_BYTES);
	memcpy(target_prefix_addr,prefix,prefix_len);

	PRINTF("initialized with prefix of length %d: ", prefix_len);
	for (size_t i = 0; i < prefix_len>>1; i++)
		printf("%02x", target_prefix_addr[i]);
	if (prefix_len & 0x1)
		printf("%x", (target_prefix_addr[(prefix_len>>1)]) >> 4);
	printf("\n");
	return true;
}

/*
 * Called each time a new address is mined.
 * This function is called by the worker thread, and its pointer argument(s)
 * are ephemeral. Don't do any allocations here, or you risk damaging
 * performance.
 *
 * All operations must be thread-safe.
 */
static void
on_iteration(const unsigned char * const addr, size_t addr_len, const uint64_t salt)
{
	const size_t my_prefix_len = get_prefix_len(addr);

	if (my_prefix_len <= longest_prefix_len) {
		return;
	}
	pthread_mutex_lock(&match_mutex);

	longest_prefix_salt = salt;
	memcpy(longest_prefix_addr, addr, ADDRESS_BYTES);
	atomic_store(&longest_prefix_len, my_prefix_len);

	pthread_mutex_unlock(&match_mutex);
	return;
}

/*
 * Called each time the 0-index worker thread prints its progress.
 *
 * You can print additional information here, however, accessing any fields
 * concurrently with on_iteration is unsafe.
 */
static void
on_progress(void)
{
	if (longest_prefix_len == 0) {
		return;
	}
	pthread_mutex_lock(&match_mutex);
	PRINTF("Longest prefix found matches %ld chars using salt ", longest_prefix_len);
	print_salt(longest_prefix_salt);
	printf(" giving address: 0x");
	for (size_t i = 0; i < ADDRESS_BYTES; i++)
		printf("%02x", longest_prefix_addr[i]);
	printf("\n");
	pthread_mutex_unlock(&match_mutex);

	return;
}

static struct plugin plugin = {
	.on_init = on_init,
	.on_interrupt = on_interrupt,
	.on_iteration = on_iteration,
	.on_progress = on_progress
};

/*
 * Register the plugin with the main program.
 */
__attribute__((constructor)) static void intermediate_main(void) {
	bool registered = register_plugin(&plugin);

	if (registered == false) {
		PRINTF("Registration of `partial-output` plugin failed\n");
		abort();
	}

	return;
}

/*
 * Free up any memory we allocated here, so LSAN doesn't report spurious leaks.
 */
__attribute__((destructor)) static void intermediate_cleanup(void) {
	return;
}
