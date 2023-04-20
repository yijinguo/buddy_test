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
    int order; //在该area内的次序
};

struct Link {
    struct Link* head;
    struct Link* tail; 
    int rank;
    int *map; //位图，记录area链状态
    int *status;
    struct Area *first_area;
};


struct Link *memory;
void *start;
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
    curr->status[split_a->order] = 1;
    curr->first_area = split_a->tail;
    if (curr->first_area != NULL) curr->first_area->head = NULL;
    curr->map[split_a->order / 2] ^= 1; 

    struct Link *small_link = curr->head;
    struct Area *first, *second;
    first = (struct Area*) malloc (sizeof(struct Area));
    second = (struct Area*) malloc (sizeof(struct Area));
    first->rank = second->rank = small_link->rank;
    first->loc = split_a->loc;
    second->loc = split_a->loc + 4 * 1024 * square(small_link->rank);
    first->order = split_a->order * 2;
    second->order = split_a->order * 2 + 1;
    first->head = NULL;
    first->tail = second;
    second->head = first;
    second->tail = NULL;
    if (small_link->first_area == NULL) {
        small_link->first_area = first;
    }
    return small_link;
}

void merge(struct Link *link, int pair){
    struct Link *new_link = link->tail;
    new_link->status[pair] = 0;
    struct Area *first, *second, *tmp = link->first_area;
    //find first, second
    while (tmp != NULL) {
        if (tmp->order == pair * 2) {
            first = tmp;
        } else if (tmp->order == pair * 2 + 1) {
            second = tmp;
        } 
        tmp = tmp->tail;
    }
    if (first->head == NULL) {
        link->first_area = first->tail;
    } else {
        first->head->tail = first->tail;
    }
    if (first->tail != NULL) first->tail->head = first->head;
    if (second->head == NULL) {
        link->first_area = second->tail;
    } else {
        second->head->tail = second->tail;
    }
    if (second->tail != NULL) second->tail->head = second->head;
    free(first), free(second);

    //modify the new link
    struct Area *new_area = (struct Area*) malloc (sizeof(struct Area));
    new_area->tail = NULL;
    new_area->loc = start + pair * 2 * 4 * 1024 * square(link->rank);
    new_area->order = pair;
    new_area->rank = link->rank + 1;
    if (new_link->first_area == NULL) {
        new_area->head = NULL;
        new_link->first_area = new_area;
    } else {
        struct Area *last = new_link->first_area;
        while (last->tail != NULL) last = last->tail;
        new_area->head = last;
        last->tail = new_area;
    }
    new_link->map[pair / 2] ^= 1;
    if (new_link->rank < maximum_rank && new_link->map[pair / 2] == 0) {
        merge(new_link, pair / 2);
    }  
}

int init_page(void *p, int pgcount){
    start = p;
    maximum_rank = Log2(pgcount) + 1;
    struct Area *init_area = (struct Area*) malloc (sizeof(struct Area));
    init_area->head = init_area->tail = NULL;
    init_area->rank = maximum_rank;
    init_area->loc = p;
    init_area->order = 0;

    memory = (struct Link*) malloc (sizeof(struct Link));
    memory->head = NULL;
    struct Link *tmp = memory;
    for (int i = 1; i < maximum_rank; ++i) {
        struct Link *new_link = (struct Link*) malloc (sizeof(struct Link));
        new_link->rank = i;
        new_link->map = malloc(sizeof(int) * pgcount);
        new_link->status = malloc(sizeof(int) * pgcount);
        for (int i = 0; i < pgcount; i++) new_link->map[i] = new_link->status[i] = 0;
        //new_link->total = 0;
        new_link->first_area = NULL;   
        tmp->tail = new_link;
        new_link->head = tmp;
        tmp = new_link; 
    }
    struct Link *last = (struct Link*) malloc (sizeof(struct Link));
    last->rank = maximum_rank;
    last->map = malloc(sizeof(int) * pgcount);
    last->status = malloc(sizeof(int) * pgcount);
    for (int i = 0; i < pgcount; i++) last->map[i] = last->status[i] = 0;
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
    link->status[alloc->order] = 1;
    void *result = alloc->loc;
    link->first_area = alloc->tail;
    if (link->first_area != NULL) link->first_area->head = NULL;
    link->map[alloc->order / 2] ^= 1;
    free(alloc);
    return result;
}

int return_pages(void *p){
    if (p == NULL || p < start || p > start + 4 * 1024 * square(maximum_rank)) return -EINVAL;
    struct Link *link = memory->tail;
    int order = (p - start) / (4 * 1024 * square(1));
    link->map[order / 2] ^= 1;
    link->status[order] = 0;
    struct Area *tmp = (struct Area*) malloc (sizeof(struct Area));
    tmp->tail = NULL;
    tmp->loc = p;
    tmp->rank = 1;
    tmp->order = order;
    if (link->first_area == NULL) {
        link->first_area = tmp;
        tmp->head = NULL;
    } else {
        struct Area *last = link->first_area;
        while (last->tail != NULL) last = last->tail;
        last->tail = tmp;
        tmp->head = last;
    }
    if (link->map[order / 2] == 0) {
        merge(link, order / 2);
    }
    return OK;
}

int query_ranks(void *p){
    struct Link *link = memory->tail;
    int order = (p - start) / (4 * 1024), rank = 0;
    while (link != NULL) {
        if (link->status[order] == 1) {
            rank = link->rank;
            break;
        }
        order /= 2;
        link = link->tail;
    }
    if (link == NULL) rank = maximum_rank;
    return rank;
}

int query_page_counts(int rank){
    if (rank < 0 || rank > maximum_rank) return -EINVAL;
    struct Link *link = memory->tail;
    while (link != NULL) {
        if (link->rank == rank) {
            break;
        } else {
            link = link->tail; 
        }
    }
    int ans = 0;
    struct Area *tmp = link->first_area;
    while (tmp != NULL) {
        ans++;
        tmp = tmp->tail;
    }
    return ans;
}

void print_link(){
    struct Link *iter = memory->tail;
    while (iter != NULL) {
        int i = 0;
        struct Area *tmp = iter->first_area;
        while (tmp != NULL) {
            i++;
            tmp = tmp->tail;
        }
        printf("rank: %d; free: %d; status: %d %d %d %d; map: %d%d\n", 
        iter->rank, i, iter->status[0], iter->status[1], iter->status[2], iter->status[3], iter->map[0], iter->map[1]);
        iter = iter->tail;
    }
    printf("\n");
}