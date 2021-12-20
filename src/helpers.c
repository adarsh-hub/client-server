#include "helpers.h"
#include "linkedlist.h"

void free_user(void *user) {
	if (user) {
		user_t *u = (user_t *) user;
		free(u->username); u->username = NULL;
		free(u->password); u->password = NULL;
		free(u); u = NULL;
	}
}

void free_auction(void *auction) {
	if (auction) {
		auction_t *a = (auction_t*) auction;
		free(a->item_name); a->item_name = NULL;
		free(a->creater); a->creater = NULL;
		if (a->highest_bidder) { 
			free(a->highest_bidder); a->highest_bidder = NULL; 
		}
		free(a); a = NULL;
	}
}

void free_job(void *job) {
	if (job) {
		job_t *j = (job_t*) job;
		deleteList(j->args);
		free(j->username); j->username = NULL;
		free(j); j = NULL;
	}
}

int auction_cmp(void *left, void *right) {
	if (left && right) {
		auction_t *l = (auction_t *)left;
		auction_t *r = (auction_t *)right;

		return (int)r->id - (int)l->id;
	} else return 0;
}

char* strjoin(list_t *args, char* delim) {
	if (args && args->head && args->head->data) {
		if (args->length == 0) return NULL;
		
		char *msg;
		node_t *curr = args->head;
		// first entry e.g. "hello\0"
		if (curr) {
			char *entry = curr->data;
			msg = malloc(strlen(entry)+1);
			msg = strcpy(msg, entry);
			curr = curr->next;
		}
		// subsequent entries e.g. "hello\r\nworld\0"
		while (curr) {
			char *entry = curr->data;
			msg = realloc(msg, strlen(msg) + strlen(delim) + strlen(entry) + 1);
			msg = strcat(msg, delim);
			msg = strcat(msg, entry);

			curr = curr->next;
		}
		return msg;
	} else return NULL;
}

list_t* strsplit(char *str, char *delim) {
	if (!str || !delim) {
		return NULL;
	}
	list_t *l = init(NULL, free);

	char *ptr = strtok(str, delim);
	insertRear(l, strdup(ptr));
	while ((ptr = strtok(NULL, delim))) {
		insertRear(l, strdup(ptr));
	}

	return l;
}

int isWatching(user_t *users_watching[5], user_t *user) {
	int i;
	for (i = 0; i < 5; i++) {
		if (users_watching[i] == user) return 1;
	}
	return 0;
}
