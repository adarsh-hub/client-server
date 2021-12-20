#include "linkedlist.h"
#include <string.h>
#include <sys/types.h>
#include <signal.h>

typedef struct {
	int type;
	unsigned int client_fd;
	char *username;
	list_t *args; // linkedlist representing the message sent by the client
} job_t;

typedef struct user {
	char *username;
	char *password;
	unsigned int fd;
	int balance;
	sig_atomic_t is_online;
} user_t;

typedef struct auction {
	char *item_name;
	unsigned int id;
	char *creater;
	char *highest_bidder;
	unsigned long bin;
	unsigned long bid;
	unsigned int rticks;
	user_t *users_watching[5];
} auction_t;

void free_user(void *user);

void free_auction(void *auction);

void free_job(void *job);

int auction_cmp(void *left, void *right);

char* strjoin(list_t *args, char *delim);

list_t* strsplit(char *str, char *delim);

int isWatching(user_t *users_watching[], user_t *user);
