/*

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                    Version 2, December 2004

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. You just DO WHAT THE FUCK YOU WANT TO.

*/

#include <stdlib.h>
#include <block_allocator.h>

block_pool_t *
new_block_pool(size_t size, size_t count) {
	int i;
	block_pool_t *bp = malloc(sizeof(block_pool_t));
	block_list_t *b;

	bp->free_blocks = malloc(size * sizeof(block_list_t));
	bp->blocks      = malloc(size * count);
	bp->size        = size;
	bp->count       = count;

	for (i = 0; i < count; i++) {
		b        = bp->free_blocks + i;
		b->next  = b + 1;
		b->block = bp->blocks + i;
	}

	b->next = NULL;

	return bp;
}

block_list_t *
get_new_block(block_pool_t *bp) {
	block_list_t *b = NULL;

	if (bp->free_blocks) {
		b = bp->free_blocks;
		bp->free_blocks = b->next;
	}

	return b;
}

void
release_block(block_pool_t *bp, block_list_t *b) {
	b->next         = bp->free_blocks;
	bp->free_blocks = b;
}

