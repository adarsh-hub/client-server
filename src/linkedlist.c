#include "linkedlist.h"

/*
    What is a linked list?
    A linked list is a set of dynamically allocated nodes, arranged in
    such a way that each node contains one value and one pointer.
    The pointer always points to the next member of the list.
    If the pointer is NULL, then it is the last node in the list.

    A linked list is held using a local pointer variable which
    points to the first item of the list. If that pointer is also NULL,
    then the list is considered to be empty.
    -------------------------------               ------------------------------              ------------------------------
    |HEAD                         |             \ |              |             |            \ |              |             |
    |                             |-------------- |     DATA     |     NEXT    |--------------|     DATA     |     NEXT    |
    |-----------------------------|             / |              |             |            / |              |             |
    |LENGTH                       |               ------------------------------              ------------------------------
    |COMPARATOR                   |
    |PRINTER                      |
    |DELETER                      |
    -------------------------------

*/

list_t* init(int (*compare)(void*, void*), void (*delete)(void*)) {
    list_t *list = (list_t *)malloc(sizeof(list_t));
    list->head = NULL;
    list->length = 0;
    list->comparator = compare;
    list->deleter = delete;
    return list;
}

void insertFront(list_t* list, void* valref) {
    if (list->length == 0)
        list->head = NULL;

    node_t** head = &(list->head);
    node_t* new_node;
    new_node = malloc(sizeof(node_t));

    new_node->data = valref;

    new_node->next = *head;
    *head = new_node;
    list->length++; 
}

void insertRear(list_t* list, void* valref) {
    if (list->length == 0) {
        insertFront(list, valref);
        return;
    }

    node_t* head = list->head;
    node_t* current = head;
    while (current->next) {
        current = current->next;
    }
    current->next = malloc(sizeof(node_t));
    current->next->data = valref;
    current->next->next = NULL;
    list->length++;
    
}

void insertInOrder(list_t* list, void* valref) {
    if (list->length == 0) {
        insertFront(list, valref);
        return;
    }

    node_t** head = &(list->head);
    node_t* new_node;
    new_node = malloc(sizeof(node_t));
    new_node->data = valref;
    new_node->next = NULL;

    if (list->comparator(new_node->data, (*head)->data) <= 0) {
        new_node->next = *head;
        *head = new_node;
    } 
    else if ((*head)->next == NULL){ 
        (*head)->next = new_node;
    }                                
    else {
        node_t* prev = *head;
        node_t* current = prev->next;
        while (current != NULL) {
            if (list->comparator(new_node->data, current->data) > 0) {
                if (current->next != NULL) {
                    prev = current;
                    current = current->next;
                } else {
                    current->next = new_node;
                    break;
                }
            } else {
                prev->next = new_node;
                new_node->next = current;
                break;
            }
        }
    }
    list->length++;
}

void* removeFront(list_t* list) {
    node_t** head = &(list->head);
    void* retval = NULL;
    node_t* next_node = NULL;

    if (list->length == 0) {
        return NULL;
    }

    next_node = (*head)->next;
    retval = (*head)->data;
    list->length--;

    node_t* temp = *head;
    *head = next_node;
    free(temp);

    return retval;
}

void* removeRear(list_t* list) {
    if (list->length == 0) {
        return NULL;
    } else if (list->length == 1) {
        return removeFront(list);
    }

    void* retval = NULL;
    node_t* head = list->head;
    node_t* current = head;

    while (current->next->next != NULL) { 
        current = current->next;
    }

    retval = current->next->data;
    free(current->next);
    current->next = NULL;

    list->length--;

    return retval;
}

/* indexed by 0 */
void* removeByIndex(list_t* list, int index) {
    if (list->length <= index) {
        return NULL;
    }

    node_t** head = &(list->head);
    void* retval = NULL;
    node_t* current = *head;
    node_t* prev = NULL;
    int i = 0;

    if (index == 0) {
        retval = (*head)->data;
        
		node_t* temp = *head;
        *head = current->next;
        free(temp);
        
		list->length--;
        return retval;
    }

    while (i++ != index) {
        prev = current;
        current = current->next;
    }

    prev->next = current->next;
    retval = current->data;
    free(current);

    list->length--;

    return retval;
}

void* getElement(list_t* list, int index) {
    if (list->length <= index) {
        return NULL;
    }

    node_t *curr = list->head;
    int i = 0;
    while (curr) {
        if (index == i) return curr->data;
        curr = curr->next;
        i++;
    }
    return NULL;
}

// void* removeByValRef(list_t *list, void* valref) {
//     if (list->length == 0) return NULL;
//     if (list->length == 1) {
//         return removeFront(list);
//     }
//     void* retval = NULL;
//     node_t* prev = list->head;

//     while (prev->next && prev->next->data != valref) {
//         prev = prev->next;
//     }
    
//     retval = (prev->next) ? prev->next->data : NULL;
//     node_t *temp = prev->next;
//     if (temp) free(temp);
//     prev->next = (prev->next) ? prev->next->next : NULL;
//     list->length--;

//     return retval;
// }

void deleteList(list_t* list) {
    if (list) {
        // node_t *curr = list->head;
        // node_t *next;
        // while (curr) {
        //     next = curr->next;
        //     list->deleter(curr->data);
        //     free(curr); curr = NULL;
        //     list->length--;
        //     curr = next;
        // }
        while (list->head){
            void *retval = removeFront(list);
            if (list->deleter) list->deleter(retval);
            // free(retval); retval = NULL;
        }
        list->length = 0;
        free(list); list = NULL;
    }
}

void sortList(list_t* list) {
    list_t* new_list = malloc(sizeof(list_t));	
    
	new_list->length = 0;
    new_list->comparator = list->comparator;
    new_list->head = NULL;

    int i = 0;
    int len = list->length;
    for (; i < len; ++i)
    {
        void* val = removeRear(list);
        insertInOrder(new_list, val); 
    }

    node_t* temp = list->head;
    list->head = new_list->head;

    new_list->head = temp;
    list->length = new_list->length;  

    deleteList(new_list);
    free(new_list);  
}
