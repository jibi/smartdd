/*

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                    Version 2, December 2004

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. You just DO WHAT THE FUCK YOU WANT TO.

*/

#include <pthread.h>

typedef struct blocking_queue_s {
	pthread_mutex_t tail_lock;
	pthread_cond_t  tail_not_full_cond;
	pthread_cond_t  tail_not_empty_cond;

	size_t size;
	size_t used;

	size_t start;
	size_t end;

	char resizable;

	void **queue;
} blocking_queue_t;

#define blocking_queue_full(q)  (q->used == q->size)
#define blocking_queue_empty(q) (q->used == 0)

blocking_queue_t *new_blocking_queue(size_t size, char resizable);
void blocking_queue_enqueue(blocking_queue_t *q, void *data);
void *blocking_queue_dequeue(blocking_queue_t *q);
