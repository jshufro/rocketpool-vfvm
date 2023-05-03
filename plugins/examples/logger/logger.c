#include <stdio.h>
#include <stdlib.h>

#include "logger.h"
#include "plugin.h"

#define PRINTF(FMT, ...) printf("(logger-plugin) " FMT, ##__VA_ARGS__)

/*
 * Called before mining begins.
 * Initialize state here- once mining begins, calls to on_iteration will come
 * from the worker threads, and must be thread-safe.
 */
static bool
on_init(const unsigned char * const prefix, uint8_t prefix_len)
{

	PRINTF("initialized with prefix of length %d.\n", prefix_len);
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
on_iteration(const unsigned char * const addr, size_t addr_len)
{

	//PRINTF("observed a newly mined address!\n");
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

	PRINTF("progress report!\n");
	return;
}

static struct plugin plugin = {
	.on_init = on_init,
	.on_iteration = on_iteration,
	.on_progress = on_progress
};

/*
 * Register the plugin with the main program.
 */
__attribute__((constructor)) static void intermediate_main(void) {
	bool registered = register_plugin(&plugin);

	if (registered == false) {
		PRINTF("Registration of `intermediate` plugin failed\n");
		abort();
	}

	PRINTF("Registration of `intermediate` plugin succeeded\n");
	return;
}

/*
 * Free up any memory we allocated here, so LSAN doesn't report spurious leaks.
 */
__attribute__((destructor)) static void intermediate_cleanup(void) {
	PRINTF("Spinning down!\n");
	return;
}
