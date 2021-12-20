#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

#define INT_MODE 0
#define STR_MODE 1

/*
 * Structre for each node of the linkedList
 *
 * data - a pointer to the data of the node. 
 * next - a pointer to the next node in the list. 
 */
typedef struct node {
    void* data;
    struct node* next;
} node_t;

/*
 * Structure for the base linkedList
 * 
 * head - a pointer to the first node in the list. NULL if length is 0.
 * length - the current length of the linkedList. Must be initialized to 0.
 * comparator - function pointer to linkedList comparator. Must be initialized!
 */
typedef struct list {
    node_t* head;
    int length;
    /* the comparator uses the values of the nodes directly (i.e function has to be type aware) */
    int (*comparator)(void*, void*);
    void (*deleter)(void*);
} list_t;

// Init list
list_t* init(int (*comparator)(void*, void*), void (*deleter)(void*));

/* 
 * Each of these functions inserts the reference to the data (valref)
 * into the linkedList list at the specified position
 *
 * @param list pointer to the linkedList struct
 * @param valref pointer to the data to insert into the linkedList
 */
void insertRear(list_t* list, void* valref);
void insertFront(list_t* list, void* valref);
void insertInOrder(list_t* list, void* valref);

/*
 * Each of these functions removes a single linkedList node from
 * the LinkedList at the specfied function position.
 * @param list pointer to the linkedList struct
 * @return a pointer to the removed list node
 */ 
void* removeFront(list_t* list);
void* removeRear(list_t* list);
void* removeByIndex(list_t* list, int n);
// void* removeByValRef(list_t *list, void* valref);

/* 
 * Free all nodes from the linkedList
 *
 * @param list pointer to the linkedList struct
 */
void deleteList(list_t* list);


void* getElement(list_t* list, int i);


/*
 * Traverse the list printing each node in the current order.
 * @param list pointer to the linkedList strut
 * @param mode STR_MODE to print node.value as a string,
 * INT_MODE to print node.value as an int
 */
// void printList(list_t* list, char mode);

#endif
