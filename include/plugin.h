#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (init_cb_t)(const unsigned char * const prefix, uint8_t prefix_len);
// Must be thread-safe!
typedef void (interrupt_cb_t)(void);
typedef void (iteration_cb_t)(const unsigned char * const addr, size_t addr_len, const uint64_t salt);
typedef void (progress_cb_t)(void);

struct plugin {
	init_cb_t *on_init;
	interrupt_cb_t *on_interrupt;
	iteration_cb_t *on_iteration;
	progress_cb_t *on_progress;
};

bool register_plugin(struct plugin *plugin);
