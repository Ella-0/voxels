#include <stdint.h>
#include <stddef.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "voct.h"

void voxel_cache_new(voxel_cache_t *self) {
	self->ptr = calloc(VOXEL_CACHE_SIZE, sizeof(*self->ptr));
	self->ring_index = 0;
}

void voxel_del(voct_node_t *tree, uint32_t x, uint32_t y, uint32_t z, uint32_t scale) {
	/* do nothing if we hit a leaf */
	if (tree->is_leaf) {
		fprintf(stderr,
			"warning: hit a leaf when trying to delete a tree node. "
			"voxel likely already gone from tree\n");
		return;
	}

	/* select a child */
	voct_node_t **child = &tree->children[x>>(tree->depth - 1) & 1][y>>(tree->depth - 1) & 1][z>>(tree->depth - 1) & 1];
	//voct_node_t **child = &tree->children[x>>(tree->depth) & 1][y>>(tree->depth) & 1][z>>(tree->depth) & 1];

	if (*child && (*child)->is_leaf && (*child)->voxel->scale == scale) {
		/* mark scale as 0 in the ring buffer so we know it's free */
		(*child)->voxel->scale = 0;
		*child = NULL;
	} else if (*child) {
		voxel_del(*child, x, y, z, scale);
	}
}

voxel_t *voxel_cache_push(voxel_cache_t *cache, voct_node_t *root) {
	voxel_t *ret = cache->ptr+cache->ring_index;

	for (size_t attempts = 0; ret->scale && attempts < 128; attempts++) {
		cache->ring_index = (cache->ring_index + 1) % VOXEL_CACHE_SIZE;
		ret = cache->ptr+cache->ring_index;
	}

	/* if scale is not 0, then voxel must already exist */
	if (ret->scale) {
		voxel_del(root, ret->x, ret->y, ret->z, ret->scale);
	}
	cache->ring_index = (cache->ring_index + 1) % VOXEL_CACHE_SIZE;
	return ret;
}

static inline uint32_t uniform_scale(uint8_t depth, block_flags_t flags) {
	return depth << 24 | depth << 16 | depth << 8 | flags;
}


static inline uint8_t get_scale_x(uint32_t val) {
	return (val >> 24) & 0xff;
}
static inline uint8_t get_scale_y(uint32_t val) {
	return (val >> 16) & 0xff;
}
static inline uint8_t get_scale_z(uint32_t val) {
	return (val >>  8) & 0xff;
}

voct_node_t *voxel_new(voxel_cache_t *cache, voct_node_t *root, uint32_t x, uint32_t y, uint32_t z, uint8_t depth) {
	voct_node_t *ret = calloc(1, sizeof(*ret));
	ret->is_leaf = true;
	ret->depth = depth;
	ret->voxel = voxel_cache_push(cache, root);
	ret->voxel->scale = uniform_scale(depth, BLOCK_FLAG_EXISTS);
	/* fix voxel to the grid */
	ret->voxel->x = x & ~((1 << depth << 1) - 1);
	ret->voxel->y = y & ~((1 << depth << 1) - 1);
	ret->voxel->z = z & ~((1 << depth << 1) - 1);
	return ret;
}

void voxel_set(voxel_cache_t *cache, voct_node_t *root, voct_node_t *tree, uint32_t x, uint32_t y, uint32_t z) {
	if (!tree->depth) {
		tree->is_leaf = true;
		tree->voxel = voxel_cache_push(cache, root);
		tree->voxel->scale = uniform_scale(0, 0);
		tree->voxel->x = x;
		tree->voxel->y = y;
		tree->voxel->z = z;
		return;
	}

	if (tree->is_leaf) {
		/* already full of blocks; no need to set */
		return;
	}

	/* select a child */
	voct_node_t **child = &tree->children[x>>(tree->depth - 1) & 1][y>>(tree->depth - 1) & 1][z>>(tree->depth - 1) & 1];

	/* selected child not generated so must generate it */
	if (!*child) {
		*child = malloc(sizeof(**child));

		(*child)->is_leaf = false;
		(*child)->depth = tree->depth - 1;
		memset((*child)->children, 0, sizeof((*child)->children));
	}

	voxel_set(cache, root, *child, x, y, z);

	/* if all children are leaf nodes, current node can become a leaf node by
	 * sacrificing children
	 */
	bool all_leaf = true;
	for (uint8_t i = 0; all_leaf && i < 8; i++) {
	 	voct_node_t *child = tree->children[(i&4) >> 2][(i&2) >> 1][i&1];
	 	all_leaf &= child && child->is_leaf;
	}

	if (all_leaf) {
		for (uint8_t i = 0; i < 8; i++) {
		 	voct_node_t **child = &tree->children[(i&4) >> 2][(i&2) >> 1][i&1];
			if (*child) {
			 	(*child)->voxel->scale = 0;
			 	free(*child);
			 	*child = NULL;
			}
		}

		tree->is_leaf = true;
		tree->voxel = voxel_cache_push(cache, root);
		tree->voxel->scale = uniform_scale(tree->depth, BLOCK_FLAG_EXISTS);

		/* fix voxel to the grid */
		tree->voxel->x = x & ~((1 << tree->depth) - 1);
		tree->voxel->y = y & ~((1 << tree->depth) - 1);
		tree->voxel->z = z & ~((1 << tree->depth) - 1);
	}
}

voxel_t *voxel_find(voct_node_t *tree, uint8_t max_depth, uint32_t x, uint32_t y, uint32_t z) {
	if (!tree) {
		return NULL;
	}

	if (tree->is_leaf) {
		return tree->voxel;
	}

	if (tree->depth > max_depth) {
		voct_node_t *child = tree->children[x>>(tree->depth - 1) & 1][y>>(tree->depth - 1) & 1][z>>(tree->depth - 1) & 1];
		return voxel_find(child, max_depth, x, y, z);
	} else {
		return NULL;
	}
}

void voxel_set_visible(voct_node_t *root, voct_node_t *tree) {
	if (!tree) {
		return;
	}

	if (tree->is_leaf) {
		uint32_t offset = 1 << tree->depth;

		if (!voxel_find(root, tree->depth, tree->voxel->x + offset, tree->voxel->y, tree->voxel->z))
			return;

		if (!voxel_find(root, tree->depth, tree->voxel->x, tree->voxel->y + offset, tree->voxel->z))
			return;

		if (!voxel_find(root, tree->depth, tree->voxel->x, tree->voxel->y, tree->voxel->z + offset))
			return;

		if (tree->voxel->x == 0 ||
			!voxel_find(root, tree->depth, tree->voxel->x - offset, tree->voxel->y, tree->voxel->z))
			return;

		if (tree->voxel->y == 0 ||
			!voxel_find(root, tree->depth, tree->voxel->x, tree->voxel->y - offset, tree->voxel->z))
			return;

		if (tree->voxel->z == 0 ||
			!voxel_find(root, tree->depth, tree->voxel->x, tree->voxel->y, tree->voxel->z - offset))
			return;

		tree->voxel->scale |= BLOCK_FLAG_HIDDEN;

	} else {
		for (uint8_t i = 0; i < 8; i++) {
		 	voct_node_t *child = tree->children[(i&4) >> 2][(i&2) >> 1][i&1];
		 	voxel_set_visible(root, child);
		}
	}
}

void voxel_greedy(voct_node_t *tree) {
	if (!tree)
		return;

	if (tree->is_leaf)
		return;


	for (uint8_t i = 0; i < 2; i++) {
		for (uint8_t j = 0; j < 2; j++) {
			voct_node_t *child_a = tree->children[i][j][0];
			voct_node_t *child_b = tree->children[i][j][1];

			if (!child_a || !child_a->is_leaf) {
				voxel_greedy(child_a);
				continue;
			}

			if (!child_b || !child_b->is_leaf) {
				voxel_greedy(child_a);
				continue;
			}

			uint8_t scale_a_x = get_scale_x(child_a->voxel->scale);
			uint8_t scale_b_x = get_scale_x(child_b->voxel->scale);

			uint8_t scale_a_y = get_scale_y(child_a->voxel->scale);
			uint8_t scale_b_y = get_scale_y(child_b->voxel->scale);

			if (scale_a_x == scale_b_x && scale_a_y == scale_b_y) {
				uint8_t scale_a_z = get_scale_z(child_a->voxel->scale);
				uint8_t scale_b_z = get_scale_z(child_b->voxel->scale);
				uint8_t scale_z = scale_a_z + scale_b_z;

				child_a->voxel->scale = scale_a_x << 24 | scale_a_y << 16 | scale_a_z << 8 | BLOCK_FLAG_EXISTS;
				child_b->voxel->scale = 0;

				puts("here");
			}
		}
	}
}

void dump_tree(voct_node_t *tree) {

	if (!tree) {
		puts("(nil)");
		return;
	}

	for (uint8_t i = 0; i < tree->depth; i++)
		putchar(' ');

	if (tree->is_leaf) {
		printf("leaf: %u, %u (%u, %u, %u)\n", tree->depth, tree->voxel->scale, tree->voxel->x, tree->voxel->y, tree->voxel->z);
		return;
	}
	printf("node: %u\n", tree->depth);
	for (uint8_t i = 0; i < 8; i++) {
	 	voct_node_t *child = tree->children[(i&4) >> 2][(i&2) >> 1][i&1];
	 	dump_tree(child);
	}
}
