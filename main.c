#include <assert.h>
#include <byteswap.h>
#include <ctype.h>
#include <errno.h>
#include <json-c/json.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include "KeccakP-1600-times8-SnP.h"
#include "plugin.h"

#define ADDRESS_BYTES 20
#define SALT_BYTES 32
#define INIT_HASH_BYTES 32
#define LANE_SIZE 8

struct thread_ctx {
	void *arena;
	size_t id;
	size_t nprocs;
};

static uint8_t prefix_len;
static unsigned char prefix[ADDRESS_BYTES];
static bool done = false;
static uint64_t reported_salt;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mux = PTHREAD_MUTEX_INITIALIZER;
static char *starting_salt;

/* Plugins- registered via attribute constructor, in a unterminated append-only list */
static struct plugin* plugins;
static size_t plugins_len;

static unsigned char
hex_to_char(char in)
{
	in = tolower(in);
	if (in >= '0' && in <= '9') {
		return (char)(in - '0');
	}

	return (char)(in - 'a' + 10);
}

static bool
is_hex(char in)
{
	in = tolower(in);
	if (in >= '0' && in <= '9') {
		return true;
	}

	if (in >= 'a' && in <= 'f') {
		return true;
	}

	return false;
}

static unsigned char
chars_to_byte(const char *in)
{
	unsigned char hi = hex_to_char(in[0]);
	unsigned char lo = hex_to_char(in[0 + 1]);

	return (hi << 4) | lo;
}

static void
parse_hex(unsigned char *dst, const char *in, size_t nstr)
{

	for (size_t i = 2; i < nstr - 1; i += 2) {
		dst[i/2 - 1] = chars_to_byte(&in[i]);
	}

	return;
}

static int
parse_prefix(const char *in)
{
	size_t len;
	size_t max;
	unsigned char *dst = prefix;
	unsigned char *write_head = dst;

	len = strlen(in);
	if (len <= 2)
		return -1;
	if (in[0] != '0')
		return -1;
	if (in[1] != 'x')
		return -1;

	memset(dst, 0, ADDRESS_BYTES);

	in = in + 2;
	len = strlen(in);

	if (len > 40)
		return -1;

	max = len % 2 == 0 ? len : len - 1;
	for (size_t i = 0; i < max; i += 2) {
		unsigned char hi = hex_to_char(in[i]);
		unsigned char lo = hex_to_char(in[i + 1]);
		unsigned char binary = (hi << 4) | lo;

		if (is_hex(in[i]) == false ||
		    is_hex(in[i + 1]) == false)
			return -1;

		*write_head = binary;
		write_head++;
	}

	if (len % 2 == 0) {
		prefix_len = len;
		return 0;
	}

	// If the prefix is an odd length, we need to copy the last half byte by hand.
	if (is_hex(in[max]) == false)
		return -1;
	*write_head = hex_to_char(in[max]) << 4;
	prefix_len = len;
	return 0;

}

static inline int
prefix_cmp(const unsigned char *a, const unsigned char *b)
{
	char hi_a;
	char hi_b;

	if (prefix_len % 2 == 0)
		return memcmp(a, b, prefix_len / 2);
	if (memcmp(a, b, (prefix_len - 1) / 2) != 0)
		return 1;
	hi_a = a[(prefix_len - 1)/2] & 0xf0;
	hi_b = b[(prefix_len - 1)/2] & 0xf0;
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

static inline void
on_iteration(const unsigned char * const addr, size_t addr_len)
{

	for (size_t i = 0; i < plugins_len; i++) {
		struct plugin *plugin = &plugins[i];

		plugin->on_iteration(addr, addr_len);
	}

	return;
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

		on_iteration(addr, ADDRESS_BYTES);
	}

	return false;
}

static uint64_t
parse_salt(void)
{
	uint64_t dst = 0;
	unsigned char *dst_buf = (unsigned char *)&dst;
	const char *buf = starting_salt;
	size_t salt_len;

	if (starting_salt == NULL)
		return 0;

	salt_len = strlen(starting_salt);

	size_t idx = 0;
	for (size_t i = salt_len - 2; i >= 2; i -= 2) {
		dst_buf[idx] = chars_to_byte(&buf[i]);
		idx++;
	}

	return dst - 1;
}

void *
thread_main(void *arg)
{
	struct thread_ctx *ctx = arg;
	uint64_t salt = parse_salt() + ctx->id * 8;
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

static unsigned char *
create_arena(const char *node_addr, const char *minipool_factory_addr, const char *init)
{
	unsigned char *out = calloc(136 * 8 * 2, sizeof(unsigned char));
	unsigned char node_address[ADDRESS_BYTES];
	unsigned char minipool_factory_address[ADDRESS_BYTES];
	unsigned char init_hash[INIT_HASH_BYTES];
	unsigned char ff = 0xff;

	unsigned char *phase1 = out;
	unsigned char *phase2 = phase1 + 136 * 8;

	parse_hex(node_address, node_addr, strlen(minipool_factory_addr));
	parse_hex(minipool_factory_address, minipool_factory_addr,
	    strlen(minipool_factory_addr));
	parse_hex(init_hash, init, strlen(init));

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
		/* Bytes 1-21 are the minipool factory address */
		memcpy(phase2 + i*136 + 1, minipool_factory_address, ADDRESS_BYTES);
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

static bool
parse_json_file(const char **node_addr,
    const char **minipool_factory_addr,
    const char **init)
{
	FILE *f;
	char *buf;
	size_t buf_bytes;
	size_t read;
	struct json_object *json;
	struct json_object *child;
	struct json_object *string;

	*node_addr = NULL;
	*minipool_factory_addr = NULL;
	*init = NULL;

	f = fopen("rocketpool.json", "r");
	if (f == NULL) {
		printf("Could not read rocketpool.json\n");
		return false;
	}

	fseek(f, 0L, SEEK_END);
	buf_bytes = ftell(f);

	fseek(f, 0L, SEEK_SET);
	buf = calloc(buf_bytes, sizeof(unsigned char));

	read = fread(buf, sizeof(unsigned char), buf_bytes, f);
	if (read != buf_bytes) {
		printf("Error reading rocketpool.json\n");
		free(buf);
		return false;
	}
	fclose(f);

	json = json_tokener_parse(buf);
	if (json == NULL) {
		printf("Could not parse json\n");
		free(buf);
		return false;
	}

	child = json_object_object_get(json, "atlas");
	if (child == NULL) {
		printf("Could not get settings. Atlas body expected.\n");
		json_object_put(json);
		free(buf);
		return false;
	}

	string = json_object_object_get(child, "nodeAddress");
	if (string == NULL) {
		printf("json data missing nodeAddress\n");
		json_object_put(json);
		free(buf);
		return false;
	}
	*node_addr = json_object_get_string(string);

	string = json_object_object_get(child, "minipoolFactoryAddress");
	if (string == NULL) {
		printf("json data missing minipoolFactoryAddress\n");
		json_object_put(json);
		free(buf);
		return false;
	}
	*minipool_factory_addr = json_object_get_string(string);

	string = json_object_object_get(child, "initHash");
	if (string == NULL) {
		printf("json data missing initHash\n");
		json_object_put(json);
		free(buf);
		return false;
	}
	*init = json_object_get_string(string);

	if (*init == NULL || *node_addr == NULL || *minipool_factory_addr == NULL) {
		printf("json data corrupt\n");
		json_object_put(json);
		free(buf);
		return false;
	}

	if (strlen(*node_addr) != 42) {
		printf("json contains invalid node address: %s\n", *node_addr);
		json_object_put(json);
		free(buf);
		return false;
	}

	if (strlen(*minipool_factory_addr) != 42) {
		printf("json contains invalid minipool factory address: %s\n", *minipool_factory_addr);
		json_object_put(json);
		free(buf);
		return false;
	}

	if (strlen(*init) != 66) {
		printf("json contains invalid init hash: %s\n", *init);
		json_object_put(json);
		free(buf);
		return false;
	}

	return true;
}

static bool
on_init(const unsigned char * const prefix, uint8_t prefix_len)
{
	bool r;

	for (size_t i = 0; i < plugins_len; i++) {
		struct plugin *plugin = &plugins[i];

		r = plugin->on_init(prefix, prefix_len);
		if (r == false) {
			printf("Plugin initialization failure\n");
			return false;
		}
	}

	return true;
}

static void
on_progress(void)
{

	for (size_t i = 0; i < plugins_len; i++) {
		struct plugin *plugin = &plugins[i];

		plugin->on_progress();
	}

	return;
}

int
main(int argc, char *argv[])
{
	size_t nprocs = get_nprocs();
	uint64_t last_reported_salt = 0;
	time_t start = time(NULL);
	struct timespec ts = {};
	const char *node_addr;
	const char *minipool_factory_addr;
	const char *init;

	if (argc != 2 && argc != 3) {
		printf("Usage: %s [prefix] [optional starting salt]\ne.g. %s 0xbeef01 0xffff\n",
		    argv[0], argv[0]);
		return 1;
	}

	/* First arg should be a prefix */
	if (parse_prefix(argv[1]) != 0) {
		printf("Invalid prefix '%s'\n", argv[1]);
		return 1;
	}

	/* Third arg should be a starting salt, if it exists */
	if (argc == 3) {
		size_t salt_len = strlen(argv[2]);
		if (salt_len <= 2 || salt_len % 2 != 0) {
			printf("Invalid starting salt %s\n", argv[2]);
			return 1;
		}

		for (size_t i = 2; i < salt_len; i++) {
			if (is_hex(argv[2][i]) == true)
				continue;
			printf("Invalid starting salt %s\n", argv[2]);
			return 1;
		}
	
		starting_salt = argv[2];
		last_reported_salt = parse_salt();
	}

	/* Read the json file */
	if (parse_json_file(&node_addr, &minipool_factory_addr, &init) == false)
		return 1;

	printf("Searching for %s using %lu threads\n", argv[1], nprocs);

	if (on_init(prefix, prefix_len) == false)
		return 1;

	pthread_t *threads = calloc(nprocs, sizeof(pthread_t));

	for (size_t i = 0; i < nprocs; i++) {
		struct thread_ctx *ctx = calloc(1, sizeof(struct thread_ctx));

		ctx->id = i;
		ctx->arena = create_arena(node_addr, minipool_factory_addr, init);
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

			last_reported_salt += diff;

			printf("At salt ");
			print_salt(last_reported_salt);
			printf("... %0.2fs (%0.2fM salts/sec)\n", elapsed, rate / 1000000);

			on_progress();
		} else if (done) {
			pthread_mutex_unlock(&mux);
			break;
		}
	}

	for (size_t i = 0; i < nprocs; i++) {
		pthread_join(threads[i], NULL);
	}

	free(threads);
	free(plugins);
	return 0;
}

bool
register_plugin(struct plugin *plugin)
{
	size_t new_length = plugins_len + 1;

	if (plugin->on_init == NULL)
		return false;

	if (plugin->on_iteration == NULL)
		return false;

	if (plugin->on_progress == NULL)
		return false;

	plugins = realloc(plugins, sizeof(struct plugin)*new_length);
	if (plugins == NULL) {
		return false;
	}
	plugins_len = new_length;

	memcpy(&plugins[new_length-1], plugin, sizeof(struct plugin));
	return true;
}
