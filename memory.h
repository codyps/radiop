#pragma once

/*
 * Tracks discontiguous byte ranges in a single structure
 */

#include <stdint.h>
#include <stdlib.h>

struct memory_range {
	size_t off;
	size_t len;
	uint8_t data[];
};

struct memory {
	RBTREE(struct memory_range, ranges);
};

void memory_init(struct memory *m);
void memory_insert(struct memory *m, char *data, size_t data_len, uint64_t offset);

/* TODO: iteration */
