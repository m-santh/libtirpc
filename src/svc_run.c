/*
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is the rpc server side idle loop
 * Wait for input, call server program.
 */
#include <pthread.h>
#include <reentrant.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>


#include <rpc/rpc.h>
#include "rpc_com.h"
#include <sys/select.h>

#include <sys/eventfd.h>
#include <execinfo.h>

#include <signal.h>

typedef struct {
       int fd;
       SVCXPRT_EXT_PRV *prv;
} activelist;

static pthread_mutex_t gmutex;
static activelist *active_pollfd = NULL;
static int active_max_pollfd = 0;

int __svc_mtmode = RPC_SVC_MT_AUTO;
int efd = 1;

extern SVCXPRT *get_svc_xprt(int sock);

void signal_handler(int sig) {
    printf("Received signal %d\n", sig);
    void *array[20];
    size_t size;

    // Get void*'s for all entries on the stack
    size = backtrace(array, 20);

    // Print all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

void print_fdlist(activelist *polllist, int size)
{
       int i;
       printf("poll list ");
       for(i = 0; i < size; i++) {
               printf(", %d ", polllist[i].fd);
       }
       printf("\n");
}

int getactivefd_index(activelist *fdlist, int fd, int size) {
       int i;
       for(i = 0; i< size; i++){
               if(fdlist[i].fd == fd)
                       return i;
       }
       return -1;
}

int getpollfd_index(struct pollfd *fdlist, int fd, int size) {
       int i;
       for(i = 0; i< size; i++){
               if(fdlist[i].fd == fd)
                       return i;
       }
       return -1;
}

int activefd_insert(int fd, SVCXPRT_EXT_PRV *prv)
{
       int i, insert = -1;
       /* update active list */
       pthread_mutex_lock(&gmutex);
       insert = getactivefd_index(active_pollfd, fd, active_max_pollfd);

       //printf("before inserting fd %d\n", fd);
       //print_fdlist(active_pollfd, active_max_pollfd);
       if(insert != -1) {
               //printf("already in active fd list %d prv = %lu new prv = %lu\n", fd, active_pollfd[i].prv, prv);
               //print_fdlist(active_pollfd, active_max_pollfd);
               warn("not able to insert active list\n");
       }
       else {
         for(i = 0; i< active_max_pollfd; i++) {
               if(active_pollfd[i].fd == -1) {
                       active_pollfd[i].fd = fd;
                       active_pollfd[i].prv = prv;
                       break;
               }
         }
         if (i == active_max_pollfd) {
               //printf("active poll list is short. droping %d\n", fd);
               warn("not able to insert active list\n");
               //print_fdlist(active_pollfd, active_max_pollfd);
         }
       }

       //printf("after inserting fd %d\n", fd);
       //print_fdlist(active_pollfd, active_max_pollfd);
       pthread_mutex_unlock(&gmutex);
       return i;
}

int activefd_remove(int fd, SVCXPRT_EXT_PRV *prv)
{
       int i;
       /* update active list */
       pthread_mutex_lock(&gmutex);
       //printf("before removing fd %d\n", fd);
       //print_fdlist(active_pollfd, active_max_pollfd);
       i = getactivefd_index(active_pollfd, fd, active_max_pollfd);
       if( i != -1) {
               if((active_pollfd[i].fd == fd) && (active_pollfd[i].prv == prv)) {
                       active_pollfd[i].fd = -1;
                       active_pollfd[i].prv = NULL;
               }
               else {
                       warn("Something is  really really worng in remove with ordering\n");
                       //printf("invalid active poll list %d\n",fd);
                       //print_fdlist(active_pollfd, active_max_pollfd);
               }
       }
       else {
               //printf("removing something not in list fd %d\n", fd);
               warn("not able to remove from active list\n");
               //print_fdlist(active_pollfd, active_max_pollfd);
       }

       //printf("after removing fd %d\n", fd);
       //print_fdlist(active_pollfd, active_max_pollfd);
       pthread_mutex_unlock(&gmutex);
       return i;
}

void * svc_getreq_thread(void *arg)
{
    SVCXPRT_EXT_PRV *prv = (SVCXPRT_EXT_PRV *)arg;
    int i, err;

    while(1) {

        pthread_mutex_lock(&prv->mutex);
       while(prv->state == THREAD_IDLE) {
            pthread_cond_wait(&prv->cond, &prv->mutex);
       }

       //printf("thread workup %lu fd = %d\n", prv->thread_id, prv->fd);
       if(prv->state == THREAD_KILL) {
               /* Now remove from active list */
               i = activefd_remove(prv->fd, prv);
               pthread_mutex_unlock(&prv->mutex);
               //printf("removing from active list\n");
               break;
       }
       else {
               prv->state = THREAD_WIP;
       }
       pthread_mutex_unlock(&prv->mutex);

       /* Check if stale state */

       //printf("1 in thread %lu\n", prv->thread_id);
       pthread_mutex_lock(&gmutex);
       i = getactivefd_index(active_pollfd, prv->fd, active_max_pollfd);
       if (i == -1) {
               warn("fd not in active list. Something is wrong, so killing thread\n");
               //printf("fd not in active list %d\n", prv->fd);
               //print_fdlist(active_pollfd, active_max_pollfd);
               pthread_mutex_unlock(&gmutex);
               break;
       }
       else if (active_pollfd[i].prv != prv) {
               /* New fd has been created for this prv, so kill thread */
               warn("new prv for same fd, so killing thread\n");
               //printf("current prv = %lu and new prv = %lu\n", prv, active_pollfd[i].prv);
               pthread_mutex_unlock(&gmutex);
               /* Now remove from active list */
               i = activefd_remove(prv->fd, prv);
               break;
       }
       pthread_mutex_unlock(&gmutex);

       //printf("2 in thread %lu\n", prv->thread_id);
       svc_getreq_common(prv->fd);
       //printf("3 in thread %lu\n", prv->thread_id);

       pthread_mutex_lock(&prv->mutex);
       /* Now remove from active list */
       i = activefd_remove(prv->fd, prv);
       //printf("4 in thread %lu\n", prv->thread_id);
       /* Check if thread needs to be killed */
       if(prv->state == THREAD_KILL) {
           pthread_mutex_unlock(&prv->mutex);
           break;
       }
       else if(prv->state == THREAD_WIP) {
               prv->state = THREAD_IDLE;
       //printf("6 in thread %lu\n", prv->thread_id);
              if (efd != -1) {
                  uint64_t u = prv->fd;
                  err = write(efd, &u, sizeof(u));
                  if(err < 0) {
                          warn("unable to write to eventfd\n");
                  }
              }
             else {
                          warn("eventfd is not set \n");
              }
       }
       pthread_mutex_unlock(&prv->mutex);
       //printf("thread going to sleep %lu fd = %d\n", prv->thread_id, prv->fd);
    }

    //warn("svc_mt_thread: Killing self thread");
    //printf("svc_mt_thread: Killing self thread %lu fd = %d", prv->thread_id, prv->fd);
    pthread_mutex_destroy(&prv->mutex);
    pthread_cond_destroy(&prv->cond);
    prv->fd = -1;
    mem_free(prv, sizeof(SVCXPRT_EXT_PRV));
    pthread_exit(0);
    return NULL;
}


SVCXPRT_EXT_PRV *prv_create(int fd) {
       SVCXPRT_EXT_PRV *prv;

       prv = (SVCXPRT_EXT_PRV *)mem_alloc(sizeof (SVCXPRT_EXT_PRV));
       if (prv == NULL) {
               warnx("svc_run: prv_create: out of memory");
               return NULL;
       }
       memset(prv, 0, sizeof (SVCXPRT_EXT_PRV));
       prv->fd = fd;
       prv->state = THREAD_IDLE;
       pthread_mutex_init(&prv->mutex, NULL);
       pthread_cond_init(&prv->cond, NULL);
       pthread_attr_init(&prv->attr);

       // Set the attribute to PTHREAD_MUTEX_RECURSIVE
       pthread_mutexattr_settype(&prv->attr, PTHREAD_MUTEX_RECURSIVE);

       pthread_attr_setdetachstate(&prv->attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&prv->thread_id, &prv->attr, svc_getreq_thread, (void *) prv) != 0) {
            warn("svc_getreqset_mt: pthread_create failed");
            return NULL;
        }
       //printf("created thread for fd %d %lu\n", fd, prv->thread_id);
       return prv;
}

void prv_send_msg(SVCXPRT_EXT_PRV *prv, ThreadState state, bool_t wait) {
       if(prv == NULL) {
               warn("svc_run: prv_send_msg: nothing to free");
               return;
       }

       if((state != THREAD_PENDING) && (state != THREAD_KILL)) {
               warn("unsupported state sending\n");
               //printf("unsupported state send %d\n", state);
               abort();
       }
       //printf("sending message state %d for thread %lu\n", state, prv->thread_id);
       pthread_mutex_lock(&prv->mutex);
       switch(prv->state) {
           case THREAD_KILL:
               /* wake up just incase */
               pthread_cond_signal(&prv->cond);
               //printf("child in kill  \n");
               break;
           case THREAD_IDLE:
               /* No message, good condition */
               prv->state = state;
               activefd_insert(prv->fd, prv);
               pthread_cond_signal(&prv->cond);
               break;
           case THREAD_PENDING:
               /* Should not be in this state, but ok. */
               prv->state = state;
               activefd_insert(prv->fd, prv);
               pthread_cond_signal(&prv->cond);
               //printf("droping message possibily\n");
               break;
           case THREAD_WIP:
               if (state != THREAD_KILL) {
                       activefd_insert(prv->fd, prv);
                       //printf("sending message when in progress...\n");
               }
               prv->state = state;
               pthread_cond_signal(&prv->cond);
               break;
           default:
               warn("Unsupported state of thread\n");
               abort();
       }
       pthread_mutex_unlock(&prv->mutex);
}

void prv_destroy(SVCXPRT_EXT_PRV *prv) {

        prv_send_msg(prv, THREAD_KILL, FALSE);
}

void
svc_getreq_poll_mt (pfdp, pollretval, last_max_pollfd)
     struct pollfd *pfdp;
     int pollretval;
     int last_max_pollfd;
{
  int fds_found, i;
  extern rwlock_t svc_fd_lock;
  SVCXPRT *xprt = NULL;
  SVCXPRT_EXT_PRV *prv = NULL;

  for (i = fds_found = 0; i < last_max_pollfd; ++i)
    {
      struct pollfd *p = &pfdp[i];

      if (p->fd != -1 && p->revents)
       {
         if(p->fd == efd) {
                 /* Ignore the event fd */
                 int err;
                 uint64_t u;
                 err = read(efd, &u, sizeof(u));
                 if (++fds_found >= pollretval)
                          break;
                 continue;
         }

          rwlock_rdlock(&svc_fd_lock);
          xprt = get_svc_xprt(p->fd);

          if (xprt != NULL) {
           prv = (SVCXPRT_EXT_PRV *)SVC_XP_PRV(xprt);
          }
         else {
           prv = NULL;
         }
          rwlock_unlock(&svc_fd_lock);

          /* fd has input waiting */
          if (p->revents & POLLNVAL) {
           if (prv != NULL) {
               prv_destroy(prv);
               SVC_XP_PRV(xprt) = NULL;
           }
           xprt_unregister (xprt);
         }
          else {
               if(xprt == NULL) {
                   svc_getreq_common (p->fd);
               }
               else if (xprt->xp_port != 0) {
                    svc_getreq_common(p->fd);
               }
                else {
                   /* Create the thread for the fd.. */
                    if (prv == NULL) {
                           prv = prv_create(p->fd);
                           if(prv == NULL) {
                               warn("svc_getreqset_mt: prv_create failed");
                           }
                           else  {
                               SVC_XP_PRV(xprt) = (void *)prv;
                           }
                    }
                   if(prv != NULL) {
                       prv_send_msg(prv, THREAD_PENDING, FALSE);
                   }
                   else {
                        svc_getreq_common(p->fd);
                   }


               }
         }

          if (++fds_found >= pollretval)
            break;
       }
    }
}


void
svc_run()
{
  extern rwlock_t svc_fd_lock;
  int i, max_pollfd;
  sigset_t set;
  struct pollfd *my_pollfd = NULL;
  int last_max_pollfd = 0;
  int mt_mode = RPC_SVC_MT_AUTO;
  // Define and set mutex attribute directly
    pthread_mutexattr_t attr;

    // Initialize the attribute variable
    pthread_mutexattr_init(&attr);

    // Set the attribute to make the mutex recursive
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    // Initialize the recursive mutex with the attribute
    pthread_mutex_init(&gmutex, &attr);

    // Unblock the signals we want to handle in this thread
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGBUS);
    sigaddset(&set, SIGABRT);
    sigaddset(&set, SIGBUS);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    // Set up the signal handler for SIGINT
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);


  pthread_mutex_lock(&gmutex);
  efd = eventfd(0,0);
  if(efd == -1) {
         warn("not able to create eventfd\n");
         pthread_mutex_unlock(&gmutex);
         return;
  }
  printf("eventfd created %d\n", efd);

  rpc_control(RPC_SVC_MTMODE_GET, &mt_mode);

  pthread_mutex_unlock(&gmutex);
  /* if mt mode, block sigpipe, sigterm and sigint from main thread.,  */
  for (;;) {
    activelist *newactive_pollfd;

    rwlock_rdlock(&svc_fd_lock);
    max_pollfd = svc_max_pollfd;
    if (max_pollfd == 0 && svc_pollfd == NULL) {
        rwlock_unlock(&svc_fd_lock);
        break;
    }

      if (last_max_pollfd != max_pollfd)
        {
          struct pollfd *new_pollfd
            = realloc (my_pollfd, sizeof (struct pollfd) * (max_pollfd +1));

          if (new_pollfd == NULL)
            {
              warn ("svc_run: - out of memory");
              rwlock_unlock(&svc_fd_lock);
              break;
            }

          my_pollfd = new_pollfd;
          last_max_pollfd = max_pollfd;
        }

      for (i = 0; i < max_pollfd; ++i)
        {
          my_pollfd[i].fd = svc_pollfd[i].fd;
          my_pollfd[i].events = svc_pollfd[i].events;
          my_pollfd[i].revents = 0;
        }
    rwlock_unlock(&svc_fd_lock);

    /* Now update poll list from active list */
    pthread_mutex_lock(&gmutex);
    newactive_pollfd = active_pollfd;
    if (active_max_pollfd != max_pollfd)
    {
           newactive_pollfd  = (activelist *)mem_alloc (sizeof (activelist) * max_pollfd);
           if (newactive_pollfd == NULL)
           {
                     warn ("svc_run: - active fd out of memory");
                      pthread_mutex_unlock(&gmutex);
                     break;
           }
           for (i = 0; i< max_pollfd; i++) {
                   newactive_pollfd[i].fd = -1;
                   newactive_pollfd[i].prv = NULL;
           }
    }

    for (i = 0; i < active_max_pollfd; ++i)
    {
         if(active_pollfd[i].fd != -1) {
             int indx = getpollfd_index(my_pollfd, active_pollfd[i].fd, max_pollfd);
             if(active_pollfd[i].prv == NULL) {
                     /* Something corrup */
                     warn("active_pollfd is corrupt\n");
                     continue;
             }
             /* If it is not present, something is really realy wrong */
             else if(indx == -1) {
                     //pthread_mutex_unlock(&gmutex);
                     //prv_destroy(active_pollfd[i].prv);
                     //pthread_mutex_lock(&gmutex);
                     warn("fd not in my_pollfd. something might have happened.\n");
                     //printf("fd not in mupoll %d thread_id %lu\n", active_pollfd[i].fd, active_pollfd[i].prv->thread_id);
                     //print_fdlist(active_pollfd, active_max_pollfd);
             }
             else {
                     /* remove from my_poll */
                     my_pollfd[indx].fd = -1;
                     /* Update only if pointers have changed */
                     if (newactive_pollfd != active_pollfd) {
                       newactive_pollfd[indx].fd = active_pollfd[i].fd;
                       newactive_pollfd[indx].prv = active_pollfd[i].prv;
                     }
             }
         }
    }

    if (newactive_pollfd != active_pollfd) {
       //printf("before list update\n");
       //print_fdlist(active_pollfd, active_max_pollfd);

       mem_free(active_pollfd, sizeof(activelist) * active_max_pollfd);
        active_pollfd = newactive_pollfd;
        active_max_pollfd = max_pollfd;
       //printf("after list update\n");
       //print_fdlist(active_pollfd, active_max_pollfd);
    }
    pthread_mutex_unlock(&gmutex);

    /* add event fd */
    my_pollfd[max_pollfd].fd = efd;
    my_pollfd[max_pollfd].events = (POLLIN | POLLPRI |
                                    POLLRDNORM | POLLRDBAND);
    my_pollfd[max_pollfd].revents = 0;

      switch (i = poll (my_pollfd, max_pollfd + 1, -1))
        {
        case -1:
          if (errno == EINTR)
            continue;
          warn ("svc_run: - poll failed");
          break;
        case 0:
          continue;
        default:
           switch (mt_mode) {
               case RPC_SVC_MT_NONE:
                   svc_getreq_poll(my_pollfd, i);
                   break;
               case RPC_SVC_MT_AUTO:
                   svc_getreq_poll_mt(my_pollfd, i, last_max_pollfd +1);
                   break;
               default:
                   warn("svc_run: invalid mt mode specified");
                   abort();
           }
          continue;
        }
      break;
  }
  warn("unexpected event in svc_run\n");

  if(my_pollfd != NULL)
     free (my_pollfd);
  if(active_pollfd != NULL)
     mem_free(active_pollfd, sizeof(activelist)*active_max_pollfd);
  active_pollfd = NULL;
  active_max_pollfd = 0;
}


/*
 *      This function causes svc_run() to exit by telling it that it has no
 *      more work to do.
 */
void
svc_exit()
{
	extern rwlock_t svc_fd_lock;

	rwlock_wrlock(&svc_fd_lock);
	free (svc_pollfd);
	svc_pollfd = NULL;
	svc_max_pollfd = 0;
	rwlock_unlock(&svc_fd_lock);
}
