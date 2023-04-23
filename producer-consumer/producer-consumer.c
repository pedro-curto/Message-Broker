#include "producer-consumer.h"
#include "extras.h"
#include <stdio.h>
#include <stdlib.h>

int pcq_create(pc_queue_t *queue, size_t capacity) {
  // initializes queue variables
  queue->pcq_capacity = capacity;
  queue->pcq_current_size = 0;
  queue->pcq_head = 0;
  queue->pcq_tail = 0;
  // stores pointers to protocol structs
  queue->pcq_buffer = malloc(capacity * sizeof(protocol *));
  // initializes mutexes and condvars
  pthread_mutex_init(&queue->pcq_current_size_lock, NULL);
  pthread_mutex_init(&queue->pcq_head_lock, NULL);
  pthread_mutex_init(&queue->pcq_tail_lock, NULL);
  pthread_mutex_init(&queue->pcq_pusher_condvar_lock, NULL);
  pthread_mutex_init(&queue->pcq_popper_condvar_lock, NULL);
  pthread_cond_init(&queue->pcq_pusher_condvar, NULL);
  pthread_cond_init(&queue->pcq_popper_condvar, NULL);
  return 0;
}

int pcq_destroy(pc_queue_t *queue) {
  // destroys mutexes, condvars, and frees the pcq_buffer pointter
  pthread_mutex_destroy(&queue->pcq_current_size_lock);
  pthread_mutex_destroy(&queue->pcq_head_lock);
  pthread_mutex_destroy(&queue->pcq_tail_lock);
  pthread_mutex_destroy(&queue->pcq_pusher_condvar_lock);
  pthread_mutex_destroy(&queue->pcq_popper_condvar_lock);
  pthread_cond_destroy(&queue->pcq_pusher_condvar);
  pthread_cond_destroy(&queue->pcq_popper_condvar);
  free(queue->pcq_buffer);
  return 0;
}

int pcq_enqueue(pc_queue_t *queue, void *elem) {
  pthread_mutex_lock(&queue->pcq_current_size_lock);
  // if the queue is full, waits until there is space available
  while (queue->pcq_current_size == queue->pcq_capacity) {
    pthread_cond_wait(&queue->pcq_pusher_condvar,
                      &queue->pcq_current_size_lock);
  }
  // locks pcq head, adds protocol message to buffer, and moves head
  // to next available space
  pthread_mutex_lock(&queue->pcq_head_lock);
  queue->pcq_buffer[queue->pcq_head] = elem;
  queue->pcq_head = (queue->pcq_head + 1) % queue->pcq_capacity;
  pthread_mutex_unlock(&queue->pcq_head_lock);
  queue->pcq_current_size++;
  // signals pcq_dequeue condvar that an element was enqueued
  pthread_cond_signal(&queue->pcq_popper_condvar);
  pthread_mutex_unlock(&queue->pcq_current_size_lock);

  return 0;
}

void *pcq_dequeue(pc_queue_t *queue) {
  void *elem;
  pthread_mutex_lock(&queue->pcq_current_size_lock);
  // if the queue is empty, waits until there's a call
  // to pcq_enqueue that adds an element
  while (queue->pcq_current_size == 0) {
    pthread_cond_wait(&queue->pcq_popper_condvar,
                      &queue->pcq_current_size_lock);
  }
  // locks pcq tail, removes protocol message from buffer, and places tail
  // on the next available space
  pthread_mutex_lock(&queue->pcq_tail_lock);
  elem = queue->pcq_buffer[queue->pcq_tail];
  queue->pcq_tail = (queue->pcq_tail + 1) % queue->pcq_capacity;
  pthread_mutex_unlock(&queue->pcq_tail_lock);
  // signals pcq_enqueue condvar that an element was dequeued
  queue->pcq_current_size--;
  pthread_cond_signal(&queue->pcq_pusher_condvar);
  pthread_mutex_unlock(&queue->pcq_current_size_lock);
  return elem;
}