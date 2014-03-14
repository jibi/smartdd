/*

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                    Version 2, December 2004

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. You just DO WHAT THE FUCK YOU WANT TO.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <queue.h>

blocking_queue_t *
new_blocking_queue(size_t size, char resizable) {
	blocking_queue_t *q = malloc(sizeof(blocking_queue_t));

	pthread_mutex_init(&q->tail_lock, NULL);
	pthread_cond_init(&q->tail_not_full_cond, NULL);
	pthread_cond_init(&q->tail_not_empty_cond, NULL);

	q->size      = size;
	q->used      = 0;
	q->start     = 0;
	q->end       = 0;
	q->resizable = resizable;

	q->queue = malloc(size * sizeof(void *));

	return q;
}

void
blocking_queue_enqueue(blocking_queue_t *q, void *data) {
	void **new_queue;

	pthread_mutex_lock(&q->tail_lock);

	if (blocking_queue_full(q)) {
		/* XXX: if we cannot allocate new memory, shall we just block or
		 * shall we abort? */
		if (q->resizable && (new_queue = malloc(q->size * 2 * sizeof(void *)))) {
			memcpy(new_queue, q->queue, q->size * sizeof(void *));
			free(q->queue);

			q->size  = q->size * 2;
			q->queue = new_queue;
		} else {
			pthread_cond_wait(&q->tail_not_full_cond, &q->tail_lock);
		}
	}

	q->queue[q->start] = data;
	q->start = (q->start + 1) % q->size;
	q->used++;

	pthread_cond_signal(&q->tail_not_empty_cond);
	pthread_mutex_unlock(&q->tail_lock);
}

void *
blocking_queue_dequeue(blocking_queue_t *q) {
	void *data;
	pthread_mutex_lock(&q->tail_lock);

	if (blocking_queue_empty(q)) {
		pthread_cond_wait(&q->tail_not_empty_cond, &q->tail_lock);
	}

	data = q->queue[q->end];
	q->end = (q->end + 1) % q->size;
	q->used--;

	pthread_cond_signal(&q->tail_not_full_cond);
	pthread_mutex_unlock(&q->tail_lock);

	return data;
}

