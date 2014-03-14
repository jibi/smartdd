/*

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                    Version 2, December 2004

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. You just DO WHAT THE FUCK YOU WANT TO.

*/

#include <unistd.h>

typedef struct block_list_s {
	void   *block;
	size_t n;
	size_t size;
	char   last;

	struct block_list_s *next;

} block_list_t;

typedef struct block_pool_s {
	size_t size;
	size_t count;

	void **blocks;
	block_list_t *free_blocks;
} block_pool_t;


block_pool_t *new_block_pool(size_t size, size_t count);
block_list_t *get_new_block(block_pool_t *bp);
void release_block(block_pool_t *bp, block_list_t *b);
