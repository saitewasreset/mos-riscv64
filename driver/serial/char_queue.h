#ifndef __CHAR_QUEUE_H
#define __CHAR_QUEUE_H

#include <mmu.h>
#include <stdbool.h>
#include <types.h>

#define BUFFER_SIZE PAGE_SIZE

struct CharQueue {
    char buffer[BUFFER_SIZE];
    u_reg_t front;
    u_reg_t rear;
    u_reg_t count;
};

void queue_init(struct CharQueue *queue);

bool is_full(struct CharQueue *queue);
bool is_empty(struct CharQueue *queue);

void enqueue(struct CharQueue *queue, char data);
char dequeue(struct CharQueue *queue);

#endif