#include "char_queue.h"

void queue_init(struct CharQueue *queue) {
    queue->count = 0;
    queue->front = 0;
    queue->rear = BUFFER_SIZE - 1;
}

bool is_full(struct CharQueue *queue) { return queue->count == BUFFER_SIZE; }

bool is_empty(struct CharQueue *queue) { return queue->count == 0; }

void enqueue(struct CharQueue *queue, char data) {
    queue->rear = (queue->rear + 1) % BUFFER_SIZE;
    queue->buffer[queue->rear] = data;

    queue->count++;
}
char dequeue(struct CharQueue *queue) {
    char data = queue->buffer[queue->front];
    queue->front = (queue->front + 1) % BUFFER_SIZE;

    queue->count--;

    return data;
}