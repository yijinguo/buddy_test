#include "buddy.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define NULL ((void *)0)

struct Area {
    struct Area *head;
    struct Area *tail;
    int rank; //用于指示大小，可能可以省略，主要是使思路更清晰
    void *loc; //内存起始位置
    int num; //在该area内的次序
};

struct Link {
    struct Link* head;
    struct Link* tail; 
    void *start;
    int rank;
    int map; //位图，记录area链状态
    int total; //目前该rank总共拥有的页面数（包含空闲+忙碌）
    struct Area *first_area;
};


struct Link *memory;
int maximum_rank;

int Log2(int n){
    int re = 0;
    while (n != 1) {
        n /= 2;
        re++;
    }
    return re;
}

int square(int rank){
    rank--;
    int ans = 1;
    while (rank > 0) {
        ans *= 2;
        rank--;
    }
    return ans;
}

struct Link* split(struct Link* curr){
    struct Area *split_a = curr->first_area;
    curr->first_area = split_a->tail;
    if (curr->first_area != NULL) curr->first_area->head = NULL;
    curr->map ^= 1 << (split_a->num / 2);

    struct Link *small_link = curr->head;

    struct Area *first, *second;
    first = (struct Area*) malloc (sizeof(struct Area));
    second = (struct Area*) malloc (sizeof(struct Area));
    first->rank = second->rank = small_link->rank;
    first->loc = split_a->loc;
    second->loc = split_a->loc + 4 * 1024 * square(small_link->rank);
    first->num = small_link->total;
    second->num = small_link->total + 1;
    first->head = NULL;
    first->tail = second;
    second->head = first;
    second->tail = NULL;
    if (small_link->total == 0) {
        small_link->start = split_a->loc;
    }
    if (small_link->first_area == NULL) {
        small_link->first_area = first;
    }
    small_link->total += 2;
    return small_link;
}

void merge(struct Link *link, int pair){
    struct Area *first = link->first_area, *second;
    while (first != NULL && first->num != pair * 2) {
        first = first->tail;
    }
    second = first->tail;
    if (first->head != NULL) first->head->tail = second->tail;
    if (second->tail != NULL) second->tail->head = first->head;
    link->total -= 2;
    link->map = link->map ^ ((link->map >> pair) << pair) + (link->map >> pair) << (pair - 1);

    struct Link *big_link = link->tail;
    struct Area *big_area = (struct Area*) malloc (sizeof(struct Area));
    big_area->rank = link->rank + 1;
    big_area->loc = first->loc;
    big_area->num = pair;
    big_link->map ^= 1 << (pair / 2);
    struct Area *tmp = NULL, *iter = big_link->first_area;
    while (iter != NULL) {
        if (iter->num < big_area->num) {
            tmp = iter;
            iter = iter->tail;
        } else {
            break;
        }
    }
    if (tmp == NULL) {
        big_link->first_area = big_area;
        big_area->head = big_area->tail = NULL;
    } else {
        tmp->tail = big_area;
        big_area->head = tmp;
        big_area->tail = iter;
        if (iter != NULL) iter->head = big_area;
    }
    
}

int init_page(void *p, int pgcount){
    maximum_rank = Log2(pgcount) + 1;
    struct Area *init_area = (struct Area*) malloc (sizeof(struct Area));
    init_area->head = init_area->tail = NULL;
    init_area->rank = maximum_rank;
    init_area->loc = p;
    init_area->num = 0;

    memory = (struct Link*) malloc (sizeof(struct Link));
    memory->head = NULL;
    struct Link *tmp = memory;
    for (int i = 0; i < maximum_rank; ++i) {
        struct Link *new_link = (struct Link*) malloc (sizeof(struct Link));
        new_link->start = 0;
        new_link->rank = i;
        new_link->map = 0;
        new_link->total = 0;
        new_link->first_area = NULL;   
        tmp->tail = new_link;
        new_link->head = tmp;
        tmp = new_link; 
    }
    struct Link *last = (struct Link*) malloc (sizeof(struct Link));
    last->start = p;
    last->rank = maximum_rank;
    last->map = 0;
    last->total = 1;
    last->first_area = init_area;
    tmp->tail = last;
    last->head = tmp;
    last->tail = NULL;
    return OK;
}

void *alloc_pages(int rank){
    if (rank < 0 || rank > maximum_rank) return (void *)(-EINVAL);
    struct Link *link = memory->tail;
    while (link != NULL) {
        if (link->rank < rank || link->rank >= rank && link->first_area == NULL) {
            link = link->tail;  
        } else {
            break;
        }
    }
    if (link == NULL) return (void*)(-ENOSPC);
    while (link->rank > rank) link = split(link);
    //Now link->rank == rank.
    struct Area *alloc = link->first_area;
    void *result = alloc->loc;
    link->first_area = alloc->tail;
    if (link->first_area != NULL) link->first_area->head = NULL;
    link->map ^= 1 << (alloc->num / 2);
    return result;
}

int return_pages(void *p){
    if (p == NULL) return -EINVAL;
    struct Link *target = memory->tail;
    while (target != NULL) {
        if (target->start + 4 * 1024 * square(target->rank) * target->total < p) {
            target = target->tail;
        } else {
            break;
        }
    }
    if (target == NULL) return -EINVAL;

    struct Area *area = (struct Area*) malloc (sizeof(struct Area));
    area->rank = target->rank;
    area->loc = p;
    area->num = (p - target->start) / (4 * 1024 * square(target->rank));

    target->map ^= 1 << (area->num / 2);
    struct Area *front = NULL, *next = target->first_area;
    while (next != NULL) {
        if (next->loc > area->loc) {
            break;
        } else {
            front = next;
            next = next->tail;
        } 
    }
    if (front == NULL) {
        target->first_area = area;
        area->head = area->tail = NULL;
    } else {
        front->tail = area;
        area->head = front;
        area->tail = next;
        if (next != NULL) next->head = area;
    }

    int pair = area->num / 2;
    if ((target->map >> pair) == 0) {
        merge(target, pair);
    }

    return OK;
}

int query_ranks(void *p){
    struct Link *link = memory->tail;
    while (link != NULL) {
        if (link->start + 4 * 1024 * square(link->rank) * link->total < p) {
            link = link->tail;
        } else {
            break;
        }
    }
    if (link == NULL) return -EINVAL;
    return link->rank;
}

int query_page_counts(int rank){
    if (rank < 0 || rank > maximum_rank) return -EINVAL;
    struct Link *link = memory->tail;
    while (link != NULL) {
        if (link->rank < rank) {
            link = link->tail; 
        } else {
            break;
        }
    }
    if (link == NULL || link->rank > rank) return 0;
    struct Area *area = link->first_area;
    int res = 0;
    while (area != NULL) {
        area = area->tail;
        res++;
    }
    return res;
}

void print_link(){
    struct Link *iter = memory->tail;
    while (iter != NULL) {
        printf("rank: %d; start: %p; total: %d; status: %d\n", iter->rank, iter->start, iter->total, iter->map);
        iter = iter->tail;
    }
}