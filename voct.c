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

voct_node_t *voxel_new(voxel_cache_t *cache, voct_node_t *root, uint32_t x, uint32_t y, uint32_t z, uint8_t depth) {
	voct_node_t *ret = calloc(1, sizeof(*ret));
	ret->is_leaf = true;
	ret->depth = depth;
	ret->voxel = voxel_cache_push(cache, root);
	ret->voxel->scale = 1 << depth;
	/* fix voxel to the grid */
	ret->voxel->x = x & ~((ret->voxel->scale << 1) - 1);
	ret->voxel->y = y & ~((ret->voxel->scale << 1) - 1);
	ret->voxel->z = z & ~((ret->voxel->scale << 1) - 1);
	return ret;
}

void voxel_set(voxel_cache_t *cache, voct_node_t *root, voct_node_t *tree, uint32_t x, uint32_t y, uint32_t z) {
	if (!tree->depth) {
		tree->is_leaf = true;
		tree->voxel = voxel_cache_push(cache, root);
		tree->voxel->scale = 1;
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
		tree->voxel->scale = 1 << tree->depth;

		/* fix voxel to the grid */
		tree->voxel->x = x & ~((tree->voxel->scale) - 1);
		tree->voxel->y = y & ~((tree->voxel->scale) - 1);
		tree->voxel->z = z & ~((tree->voxel->scale) - 1);
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



voxel_t *voxel_gen(voxel_cache_t *cache, voct_node_t *root, int32_t x, int32_t y, int32_t z, uint8_t depth) {
	voxel_t *ret = voxel_cache_push(cache, root);
	ret->x = x & (1 << (8 - depth));
	ret->y = y & (1 << (8 - depth));
	ret->z = z & (1 << (8 - depth));
	ret->scale = 1 << (8 - depth);
	return ret;
}

voxel_t *voxel_find(
	voxel_cache_t *cache,
	voct_node_t *root,
	voct_node_t *tree,
	int32_t x,
	int32_t y,
	int32_t z,
	uint8_t depth
) {
	if (depth < 8) {
		voct_node_t *child = tree->children
			[((x>(tree->voxel->x + 1) << (7 - depth)))?1:0]
			[((y>(tree->voxel->y + 1) << (7 - depth)))?1:0]
			[((z>(tree->voxel->z + 1) << (7 - depth)))?1:0];

		if (!child) {
			child = malloc(sizeof(voct_node_t));
			child->voxel = voxel_gen(cache, root, x, y, z, depth);
			memset(child->children, 0, sizeof(child->children));
		}

		return voxel_find(cache, root, child, x, y, z, depth + 1);
	} else {
		return tree->voxel;
	}
}
