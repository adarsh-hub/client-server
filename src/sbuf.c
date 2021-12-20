#include "sbuf.h"

/* Create an empty, bounded, shared FIFO buffer with n slots */
void sbuf_init(sbuf_t *sp, int n) {
	sp->buf = calloc(n, sizeof(job_t));
	sp->n = n; /* Buffer holds max of n items */
	sp->front = sp->rear = 0; /* Empty buffer iff front == rear */
	sem_init(&sp->mutex, 0, 1); /* Binary semaphore for locking */
	sem_init(&sp->slots, 0, n); /* Initially, buf has n empty slots */
	sem_init(&sp->items, 0, 0); /* Initially, buf has 0 items */
}

/* Clean up buffer sp */
void sbuf_deinit(sbuf_t *sp) {
	if (sp) {
		free(sp->buf);
		free(sp);
	}
}

/* Insert item onto the rear of shared buffer sp */
void sbuf_insert(sbuf_t *sp, void *ptr) {
	sem_wait(&sp->slots); /* Wait for available slot */
	sem_wait(&sp->mutex); /* Lock the buffer */
	sp->buf[(++sp->rear)%(sp->n)] = ptr; /* Insert the item */
	sem_post(&sp->mutex); /* Unlock the buffer */
	sem_post(&sp->items); /* Announce available item */
}

/* Remove and return the first item from buffer sp */
void* sbuf_remove(sbuf_t *sp) {
	void *ptr;
	sem_wait(&sp->items); /* Wait for available item */
	sem_wait(&sp->mutex); /* Lock the buffer */
	ptr = sp->buf[(++sp->front)%(sp->n)]; /* Remove the item */
	sem_post(&sp->mutex); /* Unlock the buffer */
	sem_post(&sp->slots); /* Announce available slot */
	return ptr;
}