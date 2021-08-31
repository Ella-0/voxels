#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum block_t {
	BLOCK_AIR = 0,
	BLOCK_RAINBOW_BARF = 1,
} block_t;

typedef struct voxel_t {
	int32_t x;
	int32_t y;
	int32_t z;
	uint32_t scale;
} voxel_t;

typedef struct voct_node_t {
	/* discriminator */
	bool is_leaf;

	/* tells us how much we need to scale voxels.
	 * the largest voxel has the largest depth
	 * the unit voxels have a depth of 0
	 */
	uint8_t depth;

	union {
		voxel_t *voxel;

		/* if any child is null, it is empty space */
		struct voct_node_t *children[2][2][2];
	};
} voct_node_t;

/* fixed sized ring buffer that keeps track of generated
 * voxels. every time a new voxel is generated it's pushed to the ring buffer
 * if there's something already there it'll go update it in the voxel tree
 * to be no longer there
 */
#define VOXEL_CACHE_SIZE (128 * 128 * 128)
typedef struct voxel_cache_t {
	voxel_t *ptr;
	size_t ring_index;
} voxel_cache_t;

void voxel_cache_new(voxel_cache_t *);
voct_node_t *voxel_new(voxel_cache_t *cache, voct_node_t *root, uint32_t x, uint32_t y, uint32_t z, uint8_t depth);
void voxel_set(voxel_cache_t *cache, voct_node_t *root, voct_node_t *tree, uint32_t x, uint32_t y, uint32_t z);

void dump_tree(voct_node_t *tree);
