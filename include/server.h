#ifndef SERVER_H
#define SERVER_H

#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdatomic.h>
#include "sbuf.h"

#define BUFFER_SIZE 1024
#define SA struct sockaddr

#define USAGE_MSG "./bin/zbid_server [-h] [-j N] [-t M] PORT_NUMBER AUCTION_FILENAME\n\n\
-h				Displays this help menu, and returns EXIT_SUCCESS\n\
-j N				Number of job threads. If option not specified, default to 2.\n\
-t M				M seconds between time ticks. If option not specified, default is to wait on\n				input from stdin to indicate a tick.\n\
PORT_NUMBER			Port number to listen on\n\
AUCTION_FILENAME		File to read auction item information from at the start of the server\n"

// Server clean up

void shutdown_server();

// Signal handlers for server

void sigint_handler(int sig);

// Server thread functions:

void* client_thread(void *clientfd_ptr);
void* job_thread();
void* tick_thread(void *ticks);

void press_to_cont();

// Server mutex functions:

void sem_enableread(sem_t *rlock, sem_t *wlock, int *rcount);
void sem_releaseread(sem_t *rlock, sem_t *wlock, int *wcount);

// Server functions:

// Initializes the server 
int server_init(int server_port);

// Main thread 
void run_server(int server_port, int num_jobthreads, int tick_speed);

#endif
