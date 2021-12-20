// Adarsh B Shankar, 11972805, shankar1@uci.edu
// Randy Quilala, 31589388, rquilala@uci.edu

#include "server.h"

// Maximum amount of concurrently running threads
#define THREADIDS_SIZE 256

// Server data structures and respective semaphores
list_t *users, *auctions;
unsigned int auctionID = 1;

sem_t users_rlock, users_wlock, auctions_rlock, auctions_wlock;
int users_rcount, auctions_rcount;

sbuf_t *job_queue;

// Currently running thread ids
pthread_t threadids[THREADIDS_SIZE];
sem_t threadids_wlock;

// Log file 
FILE *log_fileptr = NULL;
time_t clk;
sem_t logfile_wlock;

// Global listen file descriptor
int listen_fd;

void shutdown_server() {
    int i;
    sem_wait(&threadids_wlock);
    for (i = 0; i < THREADIDS_SIZE; i++) {
        pthread_cancel(threadids[i]);
    }
    close(listen_fd);
    deleteList(users);
    deleteList(auctions);
    sbuf_deinit(job_queue);
    if(log_fileptr) fclose(log_fileptr);
    exit(EXIT_SUCCESS);
    sem_post(&threadids_wlock);
}

void sigint_handler(int sig) {
    printf("Shutting down server\n");

    shutdown_server();
    
    return;
}

void *client_thread(void *clientfd_ptr) {
    int client_fd = *(int *)clientfd_ptr;
    pthread_detach(pthread_self());
    free(clientfd_ptr);

    sem_enableread(&users_rlock, &users_wlock, &users_rcount);
    node_t *curr = users->head;
    user_t *user;
    while (curr)  {
        user_t *u = curr->data;
        if (u->fd == client_fd) {
            user = u;
            break;
        }
        curr = curr->next;
    }
    sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

    if (log_fileptr) {
        sem_wait(&logfile_wlock);
        clk = time(NULL);
        fprintf(log_fileptr, "%s", ctime(&clk));
        fprintf(log_fileptr, "Client Thread (TID %ld)\n", pthread_self());
        fprintf(log_fileptr, "%s %s\n\n", "LOGIN", user->username);
        sem_post(&logfile_wlock);
    }

    while (1) {
        petr_header ph;

        if (rd_msgheader(client_fd, &ph) < 0) break;

        char buf[1024];
        read(client_fd, buf, ph.msg_len);

        if (ph.msg_type == LOGOUT) {
            ph.msg_len = 0;
            ph.msg_type = OK;
            wr_msg(client_fd, &ph, NULL);
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Client Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s\n\n", "LOGOUT", user->username);
                sem_post(&logfile_wlock);
            }
            break;
        }

        job_t *job = malloc(sizeof(job_t));
        job->type = ph.msg_type;
        job->client_fd = client_fd;
        job->username = strdup(user->username);
        job->args = (ph.msg_len) ? strsplit(buf, "\r\n") : NULL;

        sbuf_insert(job_queue, job);
    }
    user->is_online = 0;
    close(client_fd);

    return NULL;
}

void *job_thread() {
    pthread_detach(pthread_self());

    while (1) {
        job_t *job = (job_t *)sbuf_remove(job_queue);
        petr_header ph;

        if (job->type == ANCREATE) {
            if (!job->args || job->args->length != 3) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }

            char *item_name = getElement(job->args, 0);
            unsigned int duration = atoi(getElement(job->args, 1));
            unsigned long bin = atol(getElement(job->args, 2));

            auction_t *auction = (auction_t *)malloc(sizeof(auction_t));
            auction->item_name = strdup(item_name);
            auction->rticks = duration;
            auction->bin = bin;
            auction->bid = 0;
            auction->creater = strdup(job->username);
            auction->highest_bidder = NULL;

            int i;
            for (i = 0; i < 5; i++) {
                auction->users_watching[i] = NULL;
            }

            if (auction->rticks < 1 || auction->bin < 0 || strlen(auction->item_name) < 1) {
                ph.msg_len = 0;
                ph.msg_type = EINVALIDARG;
                wr_msg(job->client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s %s %d %ld\n\n", "ANCREATE:EINVALIDARG", job->username, auction->item_name, auction->rticks, auction->bin);
                    sem_post(&logfile_wlock);
                }

                free_auction(auction);
                free_job(job);
                continue;
            }

            sem_wait(&auctions_wlock);
            auction->id = auctionID++;
            insertRear(auctions, auction);
            sem_post(&auctions_wlock);

            ph.msg_type = ANCREATE;
            char num_buf[128];
            sprintf(num_buf, "%d", auction->id);
            char *msg = strdup(num_buf);
            ph.msg_len = strlen(msg) + 1;
            wr_msg(job->client_fd, &ph, msg);
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s %s %d %ld\n\n", "ANCREATE", job->username, auction->item_name, auction->rticks, auction->bin);
                sem_post(&logfile_wlock);
            }
            free(msg);
        }
        else if (job->type == ANCLOSED) {
            if (!job->args || job->args->length != 1) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }

            unsigned int auctionID = *((unsigned int *)getElement(job->args, 0));

            sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            node_t *curr = auctions->head;
            auction_t *auction;
            while (curr) {
                auction_t *a = curr->data;
                if (a->id == auctionID) {
                    auction = a;
                    break;
                }
                curr = curr->next;
            }
            sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);

            sem_enableread(&users_rlock, &users_wlock, &users_rcount);
            curr = users->head;
            user_t *user = NULL;
            user_t *creater = NULL;
            while (curr && auction->highest_bidder) {
                user_t *u = curr->data;
                if (user && creater) break;
                if (!user && !strcmp(u->username, auction->highest_bidder)) {
                    user = u;
                }
                if (!creater && !strcmp(u->username, auction->creater)) {
                    creater = u;
                }
                curr = curr->next;
            }
            sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

            if (auction->highest_bidder) {
                // The auction has a winner
                user->balance -= auction->bid;
                if (creater) creater->balance += auction->bid;

                list_t *args = init(NULL, free);
                char num_buf[128];

                sprintf(num_buf, "%d", auction->id);
                insertRear(args, strdup(num_buf));

                insertRear(args, strdup(auction->highest_bidder));

                sprintf(num_buf, "%ld", auction->bid);
                insertRear(args, strdup(num_buf));

                char *msg = strjoin(args, "\r\n");

                // Send ANCLOSED to ALL users watching the auction
                sem_enableread(&users_rlock, &users_wlock, &users_rcount);
                curr = users->head;
                while (curr) {
                    user = curr->data;
                    if (!isWatching(auction->users_watching, user))  {
                        curr = curr->next;
                        continue;
                    }
                    
                    ph.msg_len = strlen(msg) + 1;
                    ph.msg_type = ANCLOSED;
                    wr_msg(user->fd, &ph, msg);

                    curr = curr->next;
                }
                sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

                deleteList(args);
                free(msg);
            }
            else {
                // The auction did not have a winner
                char num_buf[128];

                sprintf(num_buf, "%d", auction->id);

                char *msg = strdup(num_buf);
                msg = realloc(msg, strlen(msg) + strlen("\r\n\r\n") + 1);
                msg = strcat(msg, "\r\n\r\n");

                // Send ANCLOSED to ALL users watching the auction
                sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
                sem_enableread(&users_rlock, &users_wlock, &users_rcount);
                curr = users->head;
                while (curr)
                {
                    user = curr->data;
                    if (!isWatching(auction->users_watching, user)) {
                        curr = curr->next;
                        continue;
                    }
                    
                    ph.msg_len = strlen(msg) + 1;
                    ph.msg_type = ANCLOSED;
                    wr_msg(user->fd, &ph, msg);

                    curr = curr->next;
                }
                sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
                sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

                free(msg);
            }
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %d\n\n", "ANCLOSED", auction->id);
                sem_post(&logfile_wlock);
            }
        }
        else if (job->type == ANLIST) {
            if (job->args && job->args->length != 0) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }

            sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            node_t *curr = auctions->head;
            char *msg = strdup("\0");
            while (curr) {
                auction_t *a = (auction_t *)(curr->data);
                if (a->rticks == 0) {
                    curr = curr->next;
                    continue;
                }

                list_t *l = init(NULL, free);

                char num_buf[128];
                sprintf(num_buf, "%d", a->id);
                insertRear(l, strdup(num_buf));

                insertRear(l, strdup(a->item_name));

                sprintf(num_buf, "%ld", a->bin);
                insertRear(l, strdup(num_buf));

                int i, count = 0;
                for (i = 0; i < 5; i++) {
                    if (a->users_watching[i]) count++;
                }

                sprintf(num_buf, "%d", count);
                insertRear(l, strdup(num_buf));

                sprintf(num_buf, "%ld", a->bid);
                insertRear(l, strdup(num_buf));

                sprintf(num_buf, "%d", a->rticks);
                insertRear(l, strdup(num_buf));

                char *m = strjoin(l, ";");

                m = realloc(m, strlen(m) + 1);
                m = strcat(m, "\n");

                msg = realloc(msg, strlen(msg) + strlen(m) + 1);
                msg = strcat(msg, m);
                deleteList(l);
                free(m);

                curr = curr->next;
            }
            sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);

            if (*msg) {
                ph.msg_len = strlen(msg) + 1;
                ph.msg_type = ANLIST;
                wr_msg(job->client_fd, &ph, msg);
                
            }
            else {
                ph.msg_len = 0;
                ph.msg_type = ANLIST;
                wr_msg(job->client_fd, &ph, NULL);
            }
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s\n\n", "ANLIST", job->username);
                sem_post(&logfile_wlock);
            }
            free(msg);
        }
        else if (job->type == ANWATCH) {
            if (!job->args || job->args->length != 1) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }

            unsigned int auctionID = atoi(getElement(job->args, 0));

            sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            node_t *curr = auctions->head;
            auction_t *auction = NULL;
            while (curr) {
                auction_t *a = curr->data;
                if (a->id == auctionID) {
                    auction = a;
                    break;
                }
                curr = curr->next;
            }
            sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);

            if (!auction || auction->rticks == 0) {
                ph.msg_len = 0;
                ph.msg_type = EANNOTFOUND;
                wr_msg(job->client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s %d\n\n", "ANWATCH:EANNOTFOUND", job->username, auctionID);
                    sem_post(&logfile_wlock);
                }
                
                free_job(job); job = NULL;
                continue;
            }

            int i;
            user_t **user_space = NULL;
            for (i = 0; i < 5; i++) {
                if (!auction->users_watching[i]) {
                    user_space = &auction->users_watching[i];
                    break;
                }
            }

            // If there is no "space" to watch an auction
            if (user_space == NULL) {
                ph.msg_len = 0;
                ph.msg_type = EANFULL;
                wr_msg(job->client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s %d\n\n", "ANWATCH:EANFULL", job->username, auctionID);
                    sem_post(&logfile_wlock);
                }
                free_job(job); job = NULL;
                continue;
            }

            sem_enableread(&users_rlock, &users_wlock, &users_rcount);
            curr = users->head;
            user_t *user;
            while (curr)
            {
                user_t *u = curr->data;
                if (!strcmp(u->username, job->username))
                {
                    user = u;
                    break;
                }
                curr = curr->next;
            }
            sem_releaseread(&users_rlock, &users_wlock, &users_rcount);
            
            *user_space = user;

            char num_buf[256];
            sprintf(num_buf, "%ld", auction->bin);

            list_t *args = init(NULL, free);
            insertRear(args, strdup(auction->item_name));
            insertRear(args, strdup(num_buf));

            char *msg = strjoin(args, "\r\n");

            ph.msg_len = strlen(msg) + 1;
            ph.msg_type = ANWATCH;
            wr_msg(job->client_fd, &ph, msg);
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s %d\n\n", "ANWATCH", job->username, auctionID);
                sem_post(&logfile_wlock);
            }

            free(msg);
            deleteList(args);
        }
        else if (job->type == ANLEAVE) {
            if (!job->args || job->args->length != 1)
            {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }
            
            unsigned int auctionID = atoi(getElement(job->args, 0));

            sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            node_t *curr = auctions->head;
            auction_t *auction = NULL;
            while (curr) {
                auction_t *a = curr->data;
                if (a->id == auctionID) {
                    auction = a;
                    break;
                }
                curr = curr->next;
            }
            sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);

            if (!auction || auction->rticks == 0) {
                ph.msg_len = 0;
                ph.msg_type = EANNOTFOUND;
                wr_msg(job->client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s %d\n\n", "ANLEAVE:EANNOTFOUND", job->username, (unsigned int)atoi(getElement(job->args, 0)));
                    sem_post(&logfile_wlock);
                }

                free_job(job); job = NULL;
                continue;
            }

            sem_enableread(&users_rlock, &users_wlock, &users_rcount);
            curr = users->head;
            user_t *user = NULL;
            while (curr) {
                user_t *u = curr->data;
                if (!strcmp(u->username, job->username)) {
                    user = u;
                    break;
                }
                curr = curr->next;
            }
            sem_releaseread(&users_rlock, &users_wlock, &users_rcount);
            
            int i;
            for (i = 0; i < 5; i++) {
                if (auction->users_watching[i] == user) {
                    auction->users_watching[i] = NULL;
                    break;
                }
            }

            ph.msg_len = 0;
            ph.msg_type = OK;
            wr_msg(job->client_fd, &ph, NULL);
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s %d\n\n", "ANLEAVE", job->username, auction->id);
                sem_post(&logfile_wlock);
            }
        }
        else if (job->type == ANBID) {
            if (!job->args || job->args->length != 2) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }

            int auctionID = atoi(getElement(job->args, 0));
            unsigned long bid = atol(getElement(job->args, 1));

            sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            node_t *curr = auctions->head;
            auction_t *auction = NULL;
            while (curr) {
                auction_t *a = curr->data;
                if (a->id == auctionID) {
                    auction = a;
                    break;
                }
                curr = curr->next;
            }
            sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);

            if (!auction || auction->rticks == 0) {
                ph.msg_len = 0;
                ph.msg_type = EANNOTFOUND;
                wr_msg(job->client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s %d %ld\n\n", "ANBID:EANNOTFOUND", job->username, auctionID, bid);
                    sem_post(&logfile_wlock);
                }

                free_job(job);
                continue;
            }

            sem_enableread(&users_rlock, &users_wlock, &users_rcount);
            curr = users->head;
            user_t *user = NULL;
            while (curr) {
                user_t *u = curr->data;
                if (!strcmp(u->username, job->username)) {
                    user = u;
                    break;
                }
                curr = curr->next;
            }
            sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

            if (!strcmp(job->username, auction->creater) || !isWatching(auction->users_watching, user)) {
                ph.msg_len = 0;
                ph.msg_type = EANDENIED;
                wr_msg(job->client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s %d %ld\n\n", "ANBID:EANDENIED", job->username, auctionID, bid);
                    sem_post(&logfile_wlock);
                }

                free_job(job); 
                continue;
            } 
            
            if (bid <= auction->bid) {
                ph.msg_len = 0;
                ph.msg_type = EBIDLOW;
                wr_msg(job->client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s %d %ld\n\n", "ANBID:EBIDLOW", job->username, auctionID, bid);
                    sem_post(&logfile_wlock);
                }

                free_job(job);
                continue;
            }
            else if (auction->bin != 0 && bid >= auction->bin) {
                ph.msg_len = 0;
                ph.msg_type = OK;
                wr_msg(job->client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s %d %ld\n\n", "ANBID", job->username, auctionID, bid);
                    sem_post(&logfile_wlock);
                }

                sem_wait(&auctions_wlock);
                auction->rticks = 0;
                if (auction->highest_bidder) free(auction->highest_bidder);
                auction->highest_bidder = strdup(job->username);
                auction->bid = bid;
                sem_post(&auctions_wlock);
                free_job(job); job = NULL;

                job = malloc(sizeof(job_t));
                job->type = ANCLOSED;
                job->client_fd = -1;
                job->username = NULL;
                job->args = init(NULL, NULL);
                insertRear(job->args, &auction->id);

                sbuf_insert(job_queue, job);
                continue;
            }

            // Update auction
            sem_wait(&auctions_wlock);
            auction->bid = bid;
            if (auction->highest_bidder) free(auction->highest_bidder);
            auction->highest_bidder = strdup(job->username);
            sem_post(&auctions_wlock);

            // Send ANUPDATE to ALL users
            sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            sem_enableread(&users_rlock, &users_wlock, &users_rcount);
            curr = users->head;
            while (curr) {
                user = curr->data;

                if (!isWatching(auction->users_watching, user)) {
                    curr = curr->next;
                    continue;
                }

                char auctionID[128];
                char bid_buf[128];

                sprintf(auctionID, "%d", auction->id);
                sprintf(bid_buf, "%ld", bid);

                list_t *args = init(NULL, free);
                insertRear(args, strdup(auctionID));
                insertRear(args, strdup(auction->item_name));
                insertRear(args, strdup(job->username));
                insertRear(args, strdup(bid_buf));

                char *msg = strjoin(args, "\r\n");
                ph.msg_len = strlen(msg) + 1;
                ph.msg_type = ANUPDATE;

                wr_msg(user->fd, &ph, msg);

                deleteList(args);
                free(msg);
                curr = curr->next;
            }
            sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

            ph.msg_len = 0;
            ph.msg_type = OK;
            wr_msg(job->client_fd, &ph, NULL);
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s %d %ld\n\n", "ANBID", job->username, auctionID, bid);
                sem_post(&logfile_wlock);
            }
        }
        else if (job->type == USRLIST) {
            if (job->args && job->args->length != 0) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }

            sem_enableread(&users_rlock, &users_wlock, &users_rcount);
            node_t *curr = users->head;
            list_t *usernames = init(NULL, free);
            while (curr)
            {
                user_t *u = curr->data;
                if (u->is_online && strcmp(u->username, job->username)) {
                    insertRear(usernames, strdup(u->username));
                }

                curr = curr->next;
            }
            sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

            char *msg = strjoin(usernames, "\n");
            if (msg) {
                msg = realloc(msg, strlen(msg) + 1);
                msg = strcat(msg, "\n");

                ph.msg_len = strlen(msg) + 1;
                ph.msg_type = USRLIST;
                wr_msg(job->client_fd, &ph, msg);
                free(msg);
            }
            else {
                ph.msg_len = 0;
                ph.msg_type = USRLIST;
                wr_msg(job->client_fd, &ph, NULL);
            }
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s\n\n", "USRLIST", job->username);
                sem_post(&logfile_wlock);
            }
            deleteList(usernames);
        }
        else if (job->type == USRWINS) {
            if (job->args && job->args->length != 0) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }
            char *msg = strdup("\0");
            sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            node_t *curr = auctions->head;
            while (curr) {
                auction_t *auction = curr->data;

                if (auction->highest_bidder && auction->rticks == 0 && strcmp(job->username, auction->highest_bidder)==0) {
                    list_t *args = init(NULL, free);

                    char num_buf[128];
                    sprintf(num_buf, "%d", auction->id);
                    insertRear(args, strdup(num_buf));

                    insertRear(args, strdup(auction->item_name));

                    sprintf(num_buf, "%ld", auction->bid);
                    insertRear(args, strdup(num_buf));

                    char *m = strjoin(args, ";");

                    m = realloc(m, strlen(m) + 1);
                    m = strcat(m, "\n");

                    msg = realloc(msg, strlen(msg) + strlen(m) + 1);
                    msg = strcat(msg, m);
                    deleteList(args);
                    free(m);
                }
                curr = curr->next;
            }
            sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);

            if (*msg) {
                ph.msg_len = strlen(msg) + 1;
                ph.msg_type = USRWINS;
                wr_msg(job->client_fd, &ph, msg);
            }
            else {
                ph.msg_len = 0;
                ph.msg_type = USRWINS;
                wr_msg(job->client_fd, &ph, NULL);
            }
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s\n\n", "USRWINS", job->username);
                sem_post(&logfile_wlock);
            }
            free(msg);
        }
        else if (job->type == USRSALES) {
            if (job->args && job->args->length != 0) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }
            char *msg = strdup("\0");
            sem_enableread(&auctions_rlock, &auctions_wlock, &auctions_rcount);
            node_t *curr = auctions->head;
            while (curr) {
                auction_t *auction = curr->data;

                if (auction->creater && auction->rticks == 0 && !strcmp(job->username, auction->creater)) {
                    list_t *args = init(NULL, free);

                    char num_buf[128];
                    sprintf(num_buf, "%d", auction->id);
                    insertRear(args, strdup(num_buf));

                    insertRear(args, strdup(auction->item_name));

                    if (auction->highest_bidder) {
                        insertRear(args, strdup(auction->highest_bidder));
                        sprintf(num_buf, "%ld", auction->bid);
                        insertRear(args, strdup(num_buf));
                    }
                    else {
                        insertRear(args, strdup("None"));
                        insertRear(args, strdup("None"));
                    }

                    char *m = strjoin(args, ";");

                    m = realloc(m, strlen(m) + 1);
                    m = strcat(m, "\n");

                    msg = realloc(msg, strlen(msg) + strlen(m) + 1);
                    msg = strcat(msg, m);
                    deleteList(args);
                    free(m);
                }
                curr = curr->next;
            }
            sem_releaseread(&auctions_rlock, &auctions_wlock, &auctions_rcount);

            if (*msg) {
                ph.msg_len = strlen(msg) + 1;
                ph.msg_type = USRSALES;
                wr_msg(job->client_fd, &ph, msg);
            }
            else {
                ph.msg_len = 0;
                ph.msg_type = USRSALES;
                wr_msg(job->client_fd, &ph, NULL);
            }
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s\n\n", "USRSALES", job->username);
                sem_post(&logfile_wlock);
            }
            free(msg);
        }
        else if (job->type == USRBLNC) {
            if (job->args && job->args->length != 0) {
                ph.msg_len = 0;
                ph.msg_type = ESERV;
                wr_msg(job->client_fd, &ph, NULL);
                free_job(job); job = NULL;
                continue;
            }
            char num_buf[128];
            sem_enableread(&users_rlock, &users_wlock, &users_rcount);
            node_t *curr = users->head;
            user_t *user;
            while (curr) {
                user_t *u = curr->data;

                if (!strcmp(job->username, u->username)) {
                    user = u;
                    break;
                }

                curr = curr->next;
            }
            sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

            sprintf(num_buf, "%d", user->balance);

            ph.msg_len = strlen(num_buf) + 1;
            ph.msg_type = USRBLNC;
            wr_msg(job->client_fd, &ph, num_buf);
            if (log_fileptr) {
                sem_wait(&logfile_wlock);
                clk = time(NULL);
                fprintf(log_fileptr, "%s", ctime(&clk));
                fprintf(log_fileptr, "Job Thread (TID %ld)\n", pthread_self());
                fprintf(log_fileptr, "%s %s\n\n", "USRBLNC", job->username);
                sem_post(&logfile_wlock);
            }
        }
        else {
            ph.msg_len = 0;
            ph.msg_type = ESERV;
            wr_msg(job->client_fd, &ph, NULL);
        }
        free_job(job); job = NULL;
    }
    return NULL;
}

void *tick_thread(void *ticks) {
    int tick_speed = *(int *)ticks;
    pthread_detach(pthread_self());
    free(ticks);

    int counter = 0;
    while (1) {
        if (tick_speed >= 0) sleep(tick_speed);
        else press_to_cont();

        counter++;
        if (log_fileptr) {
            sem_wait(&logfile_wlock);
            clk = time(NULL);
            fprintf(log_fileptr, "%s", ctime(&clk));
            fprintf(log_fileptr, "Tick Thread (TID %ld)\n", pthread_self());
            fprintf(log_fileptr, "%s %d\n\n", "Tick", counter);
            sem_post(&logfile_wlock);
        }

        sem_wait(&auctions_wlock);
        node_t *cur = (node_t *)auctions->head;
        while (cur) {
            auction_t *auction = (auction_t *)cur->data;
            if (auction->rticks == 0) {
                cur = cur->next;
                continue;
            }

            auction->rticks--;

            if (auction->rticks == 0) {
                job_t *job = malloc(sizeof(job_t));
                job->type = ANCLOSED;
                job->client_fd = -1;
                job->username = NULL;
                job->args = init(NULL, NULL);
                insertRear(job->args, &auction->id);

                sbuf_insert(job_queue, job);
            }
            cur = cur->next;
        }
        sem_post(&auctions_wlock);
    }
    return NULL;
}

void press_to_cont() {
    while (getchar() != '\n')
        ;
    printf("\n");
}

void sem_enableread(sem_t *rlock, sem_t *wlock, int *rcount) {
    sem_wait(rlock);
    if (++(*rcount) == 1) sem_wait(wlock);
    sem_post(rlock);
}

void sem_releaseread(sem_t *rlock, sem_t *wlock, int *rcount) {
    sem_wait(rlock);
    if (--(*rcount) == 0) sem_post(wlock);
    sem_post(rlock);
}

int server_init(int server_port) {
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    }

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA *)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    }

    // Now server is ready to listen and verification
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Currently listening on port %d.\n", server_port);

    return sockfd;
}

void run_server(int server_port, int num_jobthreads, int tick_speed) {
    listen_fd = server_init(server_port); // Initiate server and start listening on specified port
    int client_fd;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len = sizeof(client_addr);
    pthread_t tid;

    int i;
    for (i = 0; i < num_jobthreads; i++) {
        pthread_create(&tid, NULL, job_thread, NULL);
        for (i = 0; i < THREADIDS_SIZE; i++) {
            if (threadids[i] == 0) {
                threadids[i] = tid;
                break;
            }
        }
    }

    int *tick_s = malloc(sizeof(int));
    *tick_s = tick_speed;
    pthread_create(&tid, NULL, tick_thread, (void *)tick_s);
    for (i = 0; i < THREADIDS_SIZE; i++) {
        if (threadids[i] == 0) {
            threadids[i] = tid;
            break;
        }
    }

    while (1) {
        petr_header ph;

        int temp = accept(listen_fd, (SA *)&client_addr, &client_addr_len);

        if (temp < 0) {
            printf("Server accept failed\n");
            continue;
        }

        if (rd_msgheader(temp, &ph) < 0) {
            printf("Read message error\n");
            continue;
        }
        int *client_fd = malloc(sizeof(int));
        *client_fd = temp;

        char buf[1024];
        read(*client_fd, buf, ph.msg_len);

        char *username = strtok(buf, "\r\n");
        char *password = strtok(NULL, "\r\n");

        user_t *user = malloc(sizeof(user_t));
        user->username = strdup(username);
        user->password = strdup(password);
        user->is_online = 1;
        user->fd = *client_fd;
        user->balance = 0;

        sem_enableread(&users_rlock, &users_wlock, &users_rcount);
        node_t *curr = users->head;
        user_t *user_ptr = NULL;
        while (curr) {
            user_t *u = curr->data;
            if (!strcmp(u->username, user->username)) {
                user_ptr = u;
                break;
            }
            curr = curr->next;
        }
        sem_releaseread(&users_rlock, &users_wlock, &users_rcount);

        if (user_ptr) {
            // User has logged in at least once before

            free(user);

            if (user_ptr->is_online) {
                // User is already found to be logged in
                ph.msg_len = 0;
                ph.msg_type = EUSRLGDIN;
                wr_msg(*client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Main Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s\n\n", "EUSRLGDIN", user_ptr->username);
                    sem_post(&logfile_wlock);
                }

                free(client_fd);
                continue;
            }
            else if (strcmp(user_ptr->password, user->password)) {
                // Password does not match
                ph.msg_len = 0;
                ph.msg_type = EWRNGPWD;
                wr_msg(*client_fd, &ph, NULL);
                if (log_fileptr) {
                    sem_wait(&logfile_wlock);
                    clk = time(NULL);
                    fprintf(log_fileptr, "%s", ctime(&clk));
                    fprintf(log_fileptr, "Main Thread (TID %ld)\n", pthread_self());
                    fprintf(log_fileptr, "%s %s\n\n", "EWRNGPWD", user_ptr->username);
                    sem_post(&logfile_wlock);
                }

                free(client_fd);
                continue;
            }
            else {
                // User successfully logged in
                user_ptr->is_online = 1;
                user_ptr->fd = *client_fd;
            }
        }
        else {
            // New user
            user_ptr = user;
            sem_wait(&users_wlock);
            insertFront(users, user);
            sem_post(&users_wlock);
        }

        ph.msg_len = 0;
        ph.msg_type = OK;
        wr_msg(*client_fd, &ph, NULL);

        // Initializing a client thread
        sem_wait(&threadids_wlock);
        pthread_create(&tid, NULL, client_thread, (void *)client_fd);
        for (i = 0; i < THREADIDS_SIZE; i++) {
            if (threadids[i] == 0) {
                threadids[i] = tid;
                break;
            }
        }
        sem_post(&threadids_wlock);
    }
    return;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stdout, USAGE_MSG);
        return EXIT_FAILURE;
    }

    int opt, num_jobthreads = 2, tick_speed = -1;
    unsigned int port = atoi(argv[argc - 2]);

    // Parameter parsing
    while ((opt = getopt(argc, argv, "hj:t:l:")) != -1)
    {
        switch (opt) {
            case 'h':
                fprintf(stdout, USAGE_MSG);
                return EXIT_SUCCESS;
            case 'j':
                num_jobthreads = atoi(optarg);
                break;
            case 't':
                tick_speed = atoi(optarg);
                break;
            case 'l':
                log_fileptr = fopen(optarg, "w+");
                break;
            default:
                fprintf(stderr, USAGE_MSG);
                return EXIT_FAILURE;
        }
    }

    int i;
    for (i = 0; i < THREADIDS_SIZE; i++) {
        threadids[i] = 0;
    }

    // Install SIGINT handler
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("Failed to set signal handler");
        exit(EXIT_FAILURE);
    }

    // Initialize global shared variables
    users = init(NULL, free_user);
    auctions = init(auction_cmp, free_auction);
    job_queue = (sbuf_t *)malloc(sizeof(sbuf_t));
    sbuf_init(job_queue, num_jobthreads);

    // Initialize mutual exclusion read and write locks
    sem_init(&users_wlock, 0, 1);
    sem_init(&auctions_wlock, 0, 1);
    sem_init(&users_rlock, 0, 1);
    sem_init(&auctions_rlock, 0, 1);
    sem_init(&threadids_wlock, 0, 1);
    sem_init(&logfile_wlock, 0, 1);

    // Initialize auction filename into auctions list
    char *auc_filename = argv[argc - 1];
    FILE *auc_fileptr = fopen(auc_filename, "r");
    char line[1024];

    // Prefilling the server with auctions
    i = 0;
    char *item_name = NULL;
    int duration;
    int bin;
    while (fgets(line, 1024, auc_fileptr))
    {
        if (i % 4 == 0) {
            int offset = (strstr(line, "\r\n")) ? 2 : 1;
            item_name = (char *)malloc(strlen(line) - offset + 1);
            memset(item_name, 0, strlen(line) - offset + 1);
            item_name = strncpy(item_name, line, strlen(line) - offset);
        }
        else if (i % 4 == 1) {
            duration = (unsigned int)atoi(line);
        }
        else if (i % 4 == 2) {
            bin = (unsigned long)atol(line);
        }
        else {
            auction_t *auction = (auction_t *)malloc(sizeof(auction_t));
            auction->item_name = strdup(item_name);
            auction->rticks = duration;
            auction->bin = bin;
            auction->bid = 0;
            auction->creater = strdup("ZBid Server");
            auction->highest_bidder = NULL;

            int j;
            for (j = 0; j < 5; j++) {
                auction->users_watching[j] = NULL;
            }

            sem_wait(&auctions_wlock);
            auction->id = auctionID++;
            insertRear(auctions, auction);
            sem_post(&auctions_wlock);

            free(item_name); item_name = NULL;
        }
        i++;
    }
    if (item_name) free(item_name);
    fclose(auc_fileptr);

    run_server(port, num_jobthreads, tick_speed);

    return EXIT_SUCCESS;
}