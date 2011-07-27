/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/


#include <pbs_config.h>   /* the master config generated by configure */

/* define the following so we get prototype for getsid() */
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#ifdef _AIX
#include <arpa/inet.h>
#endif /* _AIX */

#include "dis.h"
#include "dis_init.h"
#include "tm.h"
#include "net_connect.h"
#include "pbs_ifl.h"


/*
** Set up a debug print macro.
*/
#ifdef  DEBUG
#define DBPRT(x) \
    { \
    int err = errno; \
    printf x; \
    errno = err; \
    }
#define DOID(x)  static char id[] = x;
#else
#define DBPRT(x)
#define DOID(x)
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/*
** Allocate some string space to hold the values passed in the
** enviornment from MOM.
*/
static char  *tm_jobid = NULL;
static int  tm_jobid_len = 0;
static char  *tm_jobcookie = NULL;
static int  tm_jobcookie_len = 0;
static tm_task_id tm_jobtid = TM_NULL_TASK;
static tm_node_id tm_jobndid = TM_ERROR_NODE;
static int  tm_momport = 0;
static int  local_conn = -1;
static int  init_done = 0;
int *tm_conn = &local_conn;

/*
** Events are the central focus of this library.  They are tracked
** in a hash table.  Many of the library calls return events.  They
** are recorded and as information is received from MOM's, the
** event is updated and marked so tm_poll() can return it to the user.
*/
#define EVENT_HASH 128

typedef struct event_info
  {
  tm_event_t  e_event; /* event number */
  tm_node_id  e_node;  /* destination node */
  int   e_mtype; /* message type sent */
  void   *e_info; /* possible returned info */

  struct event_info *e_next; /* link to next event */

  struct event_info *e_prev; /* link to prev event */
  } event_info;

static event_info *event_hash[EVENT_HASH];
static int  event_count = 0;

/*
** Find an event number or return a NULL.
*/
static event_info *
find_event(tm_event_t x)
  {
  event_info *ep;

  for (ep = event_hash[x % EVENT_HASH]; ep; ep = ep->e_next)
    {
    if (ep->e_event == x)
      break;
    }

  return ep;
  }

/*
** Delete an event.
*/
static void
del_event(event_info *ep)
  {

  /* unlink event from hash list */
  if (ep->e_prev)
    ep->e_prev->e_next = ep->e_next;
  else
    event_hash[ep->e_event % EVENT_HASH] = ep->e_next;

  if (ep->e_next)
    ep->e_next->e_prev = ep->e_prev;

  /*
  ** Free any memory saved with the event.  This depends
  ** on whay type of event it is.
  */
  switch (ep->e_mtype)
    {

    case TM_INIT:

    case TM_SPAWN:

    case TM_SIGNAL:

    case TM_OBIT:

    case TM_POSTINFO:
      break;

    case TM_TASKS:

    case TM_GETINFO:

    case TM_RESOURCES:
      free(ep->e_info);
      break;

    default:
      DBPRT(("del_event: unknown event command %d\n", ep->e_mtype))
      break;
    }

  free(ep);

  if (--event_count == 0)
    {
    close(local_conn);
    local_conn = -1;
    }

  return;
  }

/*
** Create a new event number.
*/
static tm_event_t
new_event(void)
  {
  static tm_event_t next_event = TM_NULL_EVENT + 1;
  event_info  *ep;
  tm_event_t  ret;

  if (next_event == INT_MAX)
    next_event = TM_NULL_EVENT + 1;

  for (;;)
    {
    ret = next_event++;

    for (ep = event_hash[ret % EVENT_HASH]; ep; ep = ep->e_next)
      {
      if (ep->e_event == ret)
        break; /* innter loop: this number is in use */
      }

    if (ep == NULL)
      break;  /* this number is not in use */
    }

  return ret;
  }

/*
** Link new event number into the above hash table.
*/
static void
add_event(tm_event_t event, tm_node_id node, int type, void *info)
  {
  event_info  *ep, **head;

  ep = (event_info *)malloc(sizeof(event_info));
  assert(ep != NULL);

  head = &event_hash[event % EVENT_HASH];
  ep->e_event = event;
  ep->e_node = node;
  ep->e_mtype = type;
  ep->e_info = info;
  ep->e_next = *head;
  ep->e_prev = NULL;

  if (*head)
    (*head)->e_prev = ep;

  *head = ep;

  event_count++;

  return;
  }

/*
** Sessions must be tracked by the library so tm_taskid objects
** can be resolved into real tasks on real nodes.
** We will use a hash table.
*/
#define TASK_HASH 256

typedef struct task_info
  {
  char   *t_jobid; /* jobid */
  tm_task_id   t_task; /* task id */
  tm_node_id   t_node; /* node id */

  struct task_info *t_next; /* link to next task */
  } task_info;

static task_info *task_hash[TASK_HASH];

/*
** Find a task table entry for a given task number or return a NULL.
*/
static task_info *
find_task(tm_task_id x)
  {
  task_info *tp;

  for (tp = task_hash[x % TASK_HASH]; tp; tp = tp->t_next)
    {
    if (tp->t_task == x)
      break;
    }

  return tp;
  }

/*
** Create a new task entry and link it into the above hash
** table.
*/
static tm_task_id
new_task(char *jobid, tm_node_id node, tm_task_id task)
  {
  DOID("new_task")
  task_info  *tp, **head;

  DBPRT(("%s: jobid=%s node=%d task=%lu\n",
         id, jobid, node, (unsigned long)task))

  if (jobid != tm_jobid && strcmp(jobid, tm_jobid) != 0)
    {
    DBPRT(("%s: task job %s not my job %s\n",
           id, jobid, tm_jobid))
    return TM_NULL_TASK;
    }

  if (node == TM_ERROR_NODE)
    {
    DBPRT(("%s: called with TM_ERROR_NODE\n", id))
    return TM_NULL_TASK;
    }

  if ((tp = find_task(task)) != NULL)
    {
    DBPRT(("%s: task %lu found with node %d should be %d\n",
           id, (unsigned long)task, tp->t_node, node))
    return task;
    }

  if ((tp = (task_info *)malloc(sizeof(task_info))) == NULL)
    return TM_NULL_TASK;

  head = &task_hash[task % TASK_HASH];

  tp->t_jobid = tm_jobid;

  tp->t_task  = task;

  tp->t_node  = node;

  tp->t_next = *head;

  *head = tp;

  return task;
  }

/*
** Delete a task.
===
=== right now, this is not used.
===
static void
del_task(x)
    tm_task_id x;
  {
 task_info *tp, *prev;

 prev = NULL;
 for (tp=task_hash[x % TASK_HASH]; tp; prev=tp, tp=tp->t_next) {
  if (tp->t_task == x)
   break;
 }
 if (tp) {
  if (prev)
   prev->t_next = tp->t_next;
  else
   task_hash[x % TASK_HASH] = tp->t_next;
  tp->t_next = NULL;
  if (tp->t_jobid != tm_jobid)
   free(tp->t_jobid);
  free(tp);
 }
 return;
  }
*/

/*
** The nodes are tracked in an array.
*/
static tm_node_id *node_table = NULL;


/*
** localmom() - make a connection to the local pbs_mom
**
** The connection will remain open as long as there is an
** outstanding event.
*/
#define PBS_NET_RC_FATAL -1
#define PBS_NET_RC_RETRY -2

static int
localmom(void)

  {
  static int            have_addr = 0;

  static struct in_addr hostaddr;

  struct hostent       *hp;
  int          i;

  struct sockaddr_in    remote;
  int                   sock;

  struct linger         ltime;

  if (local_conn >= 0)
    {
    return(local_conn); /* already have open connection */
    }

  if (have_addr == 0)
    {
    /* lookup "localhost" and save address */

    if ((hp = gethostbyname("localhost")) == NULL)
      {
      DBPRT(("tm_init: localhost not found\n"))

      return(-1);
      }

    assert((int)hp->h_length <= (int)sizeof(hostaddr));

    memcpy(&hostaddr, hp->h_addr_list[0], hp->h_length);

    have_addr = 1;
    }

  for (i = 0;i < 5;i++)
    {
    /* get socket */

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
      {
      return(-1);
      }

#ifndef HAVE_POLL
    if (sock >= FD_SETSIZE)
      {
      close(sock);
      return(-1);
      }

#endif

    /* make sure data goes out */

    ltime.l_onoff = 1;

    ltime.l_linger = 5;

    setsockopt(sock, SOL_SOCKET, SO_LINGER, &ltime, sizeof(ltime));

    /* connect to specified local pbs_mom and port */

    remote.sin_addr = hostaddr;

    remote.sin_port = htons((unsigned short)tm_momport);

    remote.sin_family = AF_INET;

    if (connect(sock, (struct sockaddr *)&remote, sizeof(remote)) < 0)
      {
      switch (errno)
        {

        case EINTR:

        case EADDRINUSE:

        case ETIMEDOUT:

        case ECONNREFUSED:

          close(sock);

          sleep(1);

          continue;

          /*NOTREACHED*/

          break;

        default:

          close(sock);

          return(-1);

          /*NOTREACHED*/

          break;
        }
      }
    else
      {
      local_conn = sock;

      break;
      }
    }    /* END for (i) */

  if (local_conn >= 0)
    DIS_tcp_setup(local_conn);

  return(local_conn);
  }  /* END local_mom() */





/*
** startcom() - send request header to local pbs_mom.
** If required, make connection to her.
*/

static int startcom(

  int        com,
  tm_event_t event)

  {
  int     ret;

  if (localmom() == -1)
    {
    return(-1);
    }

  ret = diswsi(local_conn, TM_PROTOCOL);

  if (ret != DIS_SUCCESS)
    goto done;

  ret = diswsi(local_conn, TM_PROTOCOL_VER);

  if (ret != DIS_SUCCESS)
    goto done;

  ret = diswcs(local_conn, tm_jobid, tm_jobid_len);

  if (ret != DIS_SUCCESS)
    goto done;

  ret = diswcs(local_conn, tm_jobcookie, tm_jobcookie_len);

  if (ret != DIS_SUCCESS)
    goto done;

  ret = diswsi(local_conn, com);

  if (ret != DIS_SUCCESS)
    goto done;

  ret = diswsi(local_conn, event);

  if (ret != DIS_SUCCESS)
    goto done;

  ret = diswui(local_conn, tm_jobtid);

  if (ret != DIS_SUCCESS)
    goto done;

  return(DIS_SUCCESS);

done:

  DBPRT(("startcom: send error %s\n",
         dis_emsg[ret]))

  close(local_conn);

  local_conn = -1;

  return(ret);
  }  /* END startcom() */




/*
** Initialize the Task Manager interface.
*/

int tm_init(

  void             *info,  /* in, currently unused */
  struct  tm_roots *roots) /* out */

  {
  tm_event_t  nevent, revent;
  char   *env, *hold;
  int   err;
  int   nerr = 0;

  if (init_done)
    {
    return(TM_BADINIT);
    }

  if ((tm_jobid = getenv("PBS_JOBID")) == NULL)
    {
    return(TM_EBADENVIRONMENT);
    }

  tm_jobid_len = strlen(tm_jobid);

  if ((tm_jobcookie = getenv("PBS_JOBCOOKIE")) == NULL)
    return TM_EBADENVIRONMENT;

  tm_jobcookie_len = strlen(tm_jobcookie);

  if ((env = getenv("PBS_NODENUM")) == NULL)
    return TM_EBADENVIRONMENT;

  tm_jobndid = (tm_node_id)strtol(env, &hold, 10);

  if (env == hold)
    return TM_EBADENVIRONMENT;

  if ((env = getenv("PBS_TASKNUM")) == NULL)
    return TM_EBADENVIRONMENT;

  if ((tm_jobtid = atoi(env)) == 0)
    return TM_EBADENVIRONMENT;

  if ((env = getenv("PBS_MOMPORT")) == NULL)
    return TM_EBADENVIRONMENT;

  if ((tm_momport = atoi(env)) == 0)
    return TM_EBADENVIRONMENT;

  init_done = 1;

  nevent = new_event();

  /*
   * send the following request:
   * header  (tm_init)
   * int  node number
   * int  task number
   */

  if (startcom(TM_INIT, nevent) != DIS_SUCCESS)
    return TM_ESYSTEM;

  DIS_tcp_wflush(local_conn);

  add_event(nevent, TM_ERROR_NODE, TM_INIT, (void *)roots);

  if ((err = tm_poll(TM_NULL_EVENT, &revent, 1, &nerr)) != TM_SUCCESS)
    return err;

  return nerr;
  }





/*
** Copy out node info.  No communication with pbs_mom is needed.
*/

int tm_nodeinfo(

  tm_node_id  **list,
  int          *nnodes)

  {
  tm_node_id *np;
  int  i;
  int  n = 0;

  if (!init_done)
    {
    return (TM_BADINIT);
    }

  if (node_table == NULL)
    {
    return (TM_ESYSTEM);
    }

  for (np = node_table; *np != TM_ERROR_NODE; np++)
    n++;  /* how many nodes */

  if ((np = (tm_node_id *)calloc(n,sizeof(tm_node_id))) == NULL)
    {
    /* FAILURE - cannot alloc memory */

    return(TM_ERROR);
    }

  for (i = 0; i < n; i++)
    np[i] = node_table[i];

  *list = np;

  *nnodes = i;

  return(TM_SUCCESS);
  }  /* END tm_nodeinfo() */





/*
** Starts <argv>[0] with environment <envp> at <where>.
*/

int tm_spawn(

  int          argc,  /* in  */
  char       **argv,  /* in  */
  char       **envp,  /* in  */
  tm_node_id   where, /* in  */
  tm_task_id  *tid,   /* out */
  tm_event_t  *event) /* out */

  {
  char *cp;
  int   i;

  /* NOTE: init_done is global */

  if (!init_done)
    {
    return(TM_BADINIT);
    }

  if ((argc <= 0) || (argv == NULL) || (argv[0] == NULL) || (*argv[0] == '\0'))
    {
    return(TM_ENOTFOUND);
    }

  *event = new_event();

  if (startcom(TM_SPAWN, *event) != DIS_SUCCESS)
    {
    return(TM_ENOTCONNECTED);
    }

  if (diswsi(local_conn, where) != DIS_SUCCESS) /* send where */
    {
    return(TM_ENOTCONNECTED);
    }

  if (diswsi(local_conn, argc) != DIS_SUCCESS) /* send argc */
    {
    return(TM_ENOTCONNECTED);
    }

  /* send argv strings across */

  for (i = 0;i < argc;i++)
    {
    cp = argv[i];

    if (diswcs(local_conn, cp, strlen(cp)) != DIS_SUCCESS)
      {
      return(TM_ENOTCONNECTED);
      }
    }

  /* send envp strings across */

  if (getenv("PBSDEBUG") != NULL)
    {
    if (diswcs(local_conn, "PBSDEBUG=1", strlen("PBSDEBUG=1")) != DIS_SUCCESS)
      {
      return(TM_ENOTCONNECTED);
      }
    }

  if (envp != NULL)
    {
    for (i = 0;(cp = envp[i]) != NULL;i++)
      {
      if (diswcs(local_conn, cp, strlen(cp)) != DIS_SUCCESS)
        {
        return(TM_ENOTCONNECTED);
        }
      }
    }

  if (diswcs(local_conn, "", 0) != DIS_SUCCESS)
    {
    return(TM_ENOTCONNECTED);
    }

  DIS_tcp_wflush(local_conn);

  add_event(*event, where, TM_SPAWN, (void *)tid);

  return(TM_SUCCESS);
  }  /* END tm_spawn() */




/*
** Sends a <sig> signal to all the process groups in the task
** signified by the handle, <tid>.
*/
int
tm_kill(
  tm_task_id tid,  /* in  */
  int sig,  /* in  */
  tm_event_t *event  /* out */
)
  {
  task_info *tp;

  if (!init_done)
    return TM_BADINIT;

  if ((tp = find_task(tid)) == NULL)
    return TM_ENOTFOUND;

  *event = new_event();

  if (startcom(TM_SIGNAL, *event) != DIS_SUCCESS)
    return TM_ENOTCONNECTED;

  if (diswsi(local_conn, tp->t_node) != DIS_SUCCESS)
    return TM_ENOTCONNECTED;

  if (diswsi(local_conn, tid) != DIS_SUCCESS)
    return TM_ENOTCONNECTED;

  if (diswsi(local_conn, sig) != DIS_SUCCESS)
    return TM_ENOTCONNECTED;

  DIS_tcp_wflush(local_conn);

  add_event(*event, tp->t_node, TM_SIGNAL, NULL);

  return TM_SUCCESS;
  }

/*
** Returns an event that can be used to learn when a task
** dies.
*/
int
tm_obit(
  tm_task_id tid,  /* in  */
  int *obitval, /* out */
  tm_event_t *event  /* out */
)
  {
  task_info *tp;

  if (!init_done)
    return TM_BADINIT;

  if ((tp = find_task(tid)) == NULL)
    return TM_ENOTFOUND;

  *event = new_event();

  if (startcom(TM_OBIT, *event) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswsi(local_conn, tp->t_node) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswsi(local_conn, tid) != DIS_SUCCESS)
    return TM_ESYSTEM;

  DIS_tcp_wflush(local_conn);

  add_event(*event, tp->t_node, TM_OBIT, (void *)obitval);

  return TM_SUCCESS;
  }

struct taskhold
  {
  tm_task_id *list;
  int  size;
  int  *ntasks;
  };

/*
** Makes a request for the list of tasks on <node>.  If <node>
** is a valid node number, it returns the event that the list of
** tasks on <node> is available.
*/
int
tm_taskinfo(
  tm_node_id node,  /* in  */
  tm_task_id *tid_list, /* out */
  int list_size, /* in  */
  int *ntasks, /* out */
  tm_event_t *event  /* out */
)
  {

  struct taskhold *thold;

  if (!init_done)
    return TM_BADINIT;

  if (tid_list == NULL || list_size == 0 || ntasks == NULL)
    return TM_EBADENVIRONMENT;

  *event = new_event();

  if (startcom(TM_TASKS, *event) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswsi(local_conn, node) != DIS_SUCCESS)
    return TM_ESYSTEM;

  DIS_tcp_wflush(local_conn);

  thold = (struct taskhold *)malloc(sizeof(struct taskhold));

  assert(thold != NULL);

  thold->list = tid_list;

  thold->size = list_size;

  thold->ntasks = ntasks;

  add_event(*event, node, TM_TASKS, (void *)thold);

  return TM_SUCCESS;
  }

/*
** Returns the job-relative node number that holds or held <tid>.  In
** case of an error, it returns TM_ERROR_NODE.
*/
int
tm_atnode(
  tm_task_id tid,  /* in  */
  tm_node_id *node  /* out */
)
  {
  task_info *tp;

  if (!init_done)
    return TM_BADINIT;

  if ((tp = find_task(tid)) == NULL)
    return TM_ENOTFOUND;

  *node = tp->t_node;

  return TM_SUCCESS;
  }

struct reschold
  {
  char *resc;
  int len;
  };

/*
** Makes a request for a string specifying the resources
** available on <node>.  If <node> is a valid node number, it
** returns the event that the string specifying the resources on
** <node> is available.  It returns ERROR_EVENT otherwise.
*/
int
tm_rescinfo(
  tm_node_id node,  /* in  */
  char *resource, /* out */
  int len,  /* in  */
  tm_event_t *event  /* out */
)
  {

  struct reschold *rhold;

  if (!init_done)
    return TM_BADINIT;

  if (resource == NULL || len == 0)
    return TM_EBADENVIRONMENT;

  *event = new_event();

  if (startcom(TM_RESOURCES, *event) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswsi(local_conn, node) != DIS_SUCCESS)
    return TM_ESYSTEM;

  DIS_tcp_wflush(local_conn);

  rhold = (struct reschold *)malloc(sizeof(struct reschold));

  assert(rhold != NULL);

  rhold->resc = resource;

  rhold->len = len;

  add_event(*event, node, TM_RESOURCES, (void *)rhold);

  return TM_SUCCESS;
  }

/*
** Posts the first <nbytes> of a copy of *<info> within MOM on
** this node, and associated with this task.  If <info> is
** non-NULL, it returns the event that the effort to post *<info>
** is complete.  It returns ERROR_EVENT otherwise.
*/
int
tm_publish(
  char *name,  /* in  */
  void *info,  /* in  */
  int len,  /* in  */
  tm_event_t *event  /* out */
)
  {

  if (!init_done)
    return TM_BADINIT;

  *event = new_event();

  if (startcom(TM_POSTINFO, *event) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswst(local_conn, name) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswcs(local_conn, info, len) != DIS_SUCCESS)
    return TM_ESYSTEM;

  DIS_tcp_wflush(local_conn);

  add_event(*event, TM_ERROR_NODE, TM_POSTINFO, NULL);

  return TM_SUCCESS;
  }

struct infohold
  {
  void *info;
  int len;
  int *info_len;
  };

/*
** Makes a request for a copy of the info posted by <tid>.  If
** <tid> is a valid task, it returns the event that the
** string specifying the info posted by <tid> is available.
*/
int
tm_subscribe(
  tm_task_id tid,  /* in  */
  char *name,  /* in  */
  void *info,  /* out */
  int len,  /* in  */
  int *info_len, /* out */
  tm_event_t *event  /* out */
)
  {
  task_info  *tp;

  struct infohold *ihold;

  if (!init_done)
    return TM_BADINIT;

  if ((tp = find_task(tid)) == NULL)
    return TM_ENOTFOUND;

  *event = new_event();

  if (startcom(TM_GETINFO, *event) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswsi(local_conn, tp->t_node) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswsi(local_conn, tid) != DIS_SUCCESS)
    return TM_ESYSTEM;

  if (diswst(local_conn, name) != DIS_SUCCESS)
    return TM_ESYSTEM;

  DIS_tcp_wflush(local_conn);

  ihold = (struct infohold *)malloc(sizeof(struct infohold));

  assert(ihold != NULL);

  ihold->info = info;

  ihold->len = len;

  ihold->info_len = info_len;

  add_event(*event, tp->t_node, TM_GETINFO, (void *)ihold);

  return TM_SUCCESS;
  }

/*
** tm_finalize() - close out task manager interface
**
** This function should be the last one called.  It is illegal to call
** any other task manager function following this one.   All events are
** freed and any connection to the task manager (pbs_mom) is closed.
** This call is synchronous.
*/
int
tm_finalize(void)
  {
  event_info *e;
  int   i = 0;

  if (!init_done)
    return TM_BADINIT;

  while (event_count && (i < EVENT_HASH))
    {
    while ((e = event_hash[i]) != NULL)
      {
      del_event(e);
      }

    ++i; /* check next slot in hash table */
    }

  init_done = 0;

  return TM_SUCCESS; /* what else */
  }

/*
** tm_notify() - set the signal to be sent on event arrival.
*/
int
tm_notify(int tm_signal)
  {
  if (!init_done)
    return TM_BADINIT;

  return TM_ENOTIMPLEMENTED;
  }

/*
** tm_alloc() - make a request for additional resources.
*/
int
tm_alloc(char *resources, tm_event_t *event)
  {
  if (!init_done)
    return TM_BADINIT;

  return TM_ENOTIMPLEMENTED;
  }

/*
** tm_dealloc() - drop a node from the job.
*/
int
tm_dealloc(tm_node_id node, tm_event_t *event)
  {
  if (!init_done)
    return TM_BADINIT;

  return TM_ENOTIMPLEMENTED;
  }

/*
** tm_create_event() - create a persistent event.
*/
int
tm_create_event(tm_event_t *event)
  {
  if (!init_done)
    return TM_BADINIT;

  return TM_ENOTIMPLEMENTED;
  }

/*
** tm_destroy_event() - destroy a persistent event.
*/
int
tm_destroy_event(tm_event_t *event)
  {
  if (!init_done)
    return TM_BADINIT;

  return TM_ENOTIMPLEMENTED;
  }

/*
** tm_register() - link a persistent event with action requests
**  from the task manager.
*/
int
tm_register(tm_whattodo_t *what, tm_event_t *event)
  {
  if (!init_done)
    return TM_BADINIT;

  return TM_ENOTIMPLEMENTED;
  }

#define FOREVER 2592000

/*
** tm_poll - poll to see if an event has be completed.
**
** If "poll_event" is a valid event handle, see if it is completed;
** else if "poll_event" is the null event, check for the first event that
** is completed.
**
** result_event is set to the completed event or the null event.
**
** If wait is non_zero, wait for "poll_event" to be completed.
**
** If an error ocurs, set tm_errno non-zero.
*/

int tm_poll(

  tm_event_t poll_event,
  tm_event_t *result_event,
  int  wait,
  int  *tm_errno)

  {
  DOID("tm_poll")

  int  num, i;
  int  ret, mtype, nnodes;
  int  prot, protver;
  int  *obitvalp;
  event_info *ep = NULL;
  tm_task_id tid, *tidp;
  tm_event_t nevent;
  tm_node_id node;
  char  *jobid;
  char  *info;

  struct tm_roots *roots;

  struct taskhold *thold;

  struct infohold *ihold;

  struct reschold *rhold;
  extern time_t pbs_tcp_timeout;

  if (!init_done)
    {
    return(TM_BADINIT);
    }

  if (result_event == NULL)
    return(TM_EBADENVIRONMENT);

  *result_event = TM_ERROR_EVENT;

  if (poll_event != TM_NULL_EVENT)
    return(TM_ENOTIMPLEMENTED);

  if (tm_errno == NULL)
    return(TM_EBADENVIRONMENT);

  if (event_count == 0)
    {
    DBPRT(("%s: no events waiting\n",
           id))

    return(TM_ENOTFOUND);
    }

  if (local_conn < 0)
    {
    DBPRT(("%s: INTERNAL ERROR %d events but no connection\n",
           id, event_count))

    return(TM_ENOTCONNECTED);
    }

  /*
  ** Setup tcp dis routines with a wait value appropriate for
  ** the value of wait the user set.
  */
  pbs_tcp_timeout = wait ? FOREVER : 0;

  prot = disrsi(local_conn, &ret);

  if (ret == DIS_EOD)
    {
    *result_event = TM_NULL_EVENT;
    return TM_SUCCESS;
    }
  else if (ret != DIS_SUCCESS)
    {
    DBPRT(("%s: protocol number dis error %d\n", id, ret))
    goto err;
    }

  if (prot != TM_PROTOCOL)
    {
    DBPRT(("%s: bad protocol number %d\n", id, prot))
    goto err;
    }

  /*
  ** We have seen the start of a message.  Set the timeout value
  ** so we wait for the remaining data of a message.
  */
  pbs_tcp_timeout = FOREVER;

  protver = disrsi(local_conn, &ret);

  if (ret != DIS_SUCCESS)
    {
    DBPRT(("%s: protocol version dis error %d\n", id, ret))
    goto err;
    }

  if (protver != TM_PROTOCOL_VER)
    {
    DBPRT(("%s: bad protocol version %d\n", id, protver))
    goto err;
    }

  mtype = disrsi(local_conn, &ret);

  if (ret != DIS_SUCCESS)
    {
    DBPRT(("%s: mtype dis error %d\n", id, ret))
    goto err;
    }

  nevent = disrsi(local_conn, &ret);

  if (ret != DIS_SUCCESS)
    {
    DBPRT(("%s: event dis error %d\n", id, ret))
    goto err;
    }

  *result_event = nevent;

  DBPRT(("%s: got event %d return %d\n", id, nevent, mtype))

  if ((ep = find_event(nevent)) == NULL)
    {
    DBPRT(("%s: No event found for number %d\n", id, nevent));
    (void)close(local_conn);
    local_conn = -1;
    return TM_ENOEVENT;
    }

  if (mtype == TM_ERROR)   /* problem, read error num */
    {
    *tm_errno = disrsi(local_conn, &ret);
    DBPRT(("%s: event %d error %d\n", id, nevent, *tm_errno));
    goto done;
    }

  *tm_errno = TM_SUCCESS;

  switch (ep->e_mtype)
    {

      /*
      ** auxiliary info (
      **  number of nodes int;
      **  nodeid[0] int;
      **  ...
      **  nodeid[n-1] int;
      **  parent jobid string;
      **  parent nodeid int;
      **  parent taskid int;
      ** )
      */

    case TM_INIT:
      nnodes = disrsi(local_conn, &ret);

      if (ret != DIS_SUCCESS)
        {
        DBPRT(("%s: INIT failed nnodes\n", id))
        goto err;
        }

      node_table = (tm_node_id *)calloc(nnodes + 1,

                                        sizeof(tm_node_id));

      if (node_table == NULL)
        {
        perror("Memory allocation failed");
        goto err;
        }

      DBPRT(("%s: INIT nodes %d\n", id, nnodes))

      for (i = 0; i < nnodes; i++)
        {
        node_table[i] = disrsi(local_conn, &ret);

        if (ret != DIS_SUCCESS)
          {
          DBPRT(("%s: INIT failed nodeid %d\n", id, i))
          goto err;
          }
        }

      node_table[nnodes] = TM_ERROR_NODE;

      jobid = disrst(local_conn, &ret);

      if (ret != DIS_SUCCESS)
        {
        DBPRT(("%s: INIT failed jobid\n", id))
        goto err;
        }

      DBPRT(("%s: INIT daddy jobid %s\n", id, jobid))

      node = disrsi(local_conn, &ret);

      if (ret != DIS_SUCCESS)
        {
        DBPRT(("%s: INIT failed parent nodeid\n", id))
        goto err;
        }

      DBPRT(("%s: INIT daddy node %d\n", id, node))

      tid = disrsi(local_conn, &ret);

      if (ret != DIS_SUCCESS)
        {
        DBPRT(("%s: INIT failed parent taskid\n", id))
        goto err;
        }

      DBPRT(("%s: INIT daddy tid %lu\n", id, (unsigned long)tid))

      roots = (struct tm_roots *)ep->e_info;
      roots->tm_parent = new_task(jobid, node, tid);
      roots->tm_me = new_task(tm_jobid,
                              tm_jobndid,
                              tm_jobtid);
      roots->tm_nnodes = nnodes;
      roots->tm_ntasks = 0;  /* TODO */
      roots->tm_taskpoolid = -1; /* what? */
      roots->tm_tasklist = NULL; /* TODO */

      break;

    case TM_TASKS:
      thold = (struct taskhold *)ep->e_info;
      tidp = thold->list;
      num = thold->size;

      for (i = 0;; i++)
        {
        tid = disrsi(local_conn, &ret);

        if (tid == TM_NULL_TASK)
          break;

        if (ret != DIS_SUCCESS)
          goto err;

        if (i < num)
          {
          tidp[i] = new_task(tm_jobid,
                             ep->e_node, tid);
          }
        }

      if (i < num)
        tidp[i] = TM_NULL_TASK;

      *(thold->ntasks) = i;

      break;

    case TM_SPAWN:
      tid = disrsi(local_conn, &ret);

      if (ret != DIS_SUCCESS)
        {
        DBPRT(("%s: SPAWN failed tid\n", id))
        goto err;
        }

      tidp = (tm_task_id *)ep->e_info;

      *tidp = new_task(tm_jobid, ep->e_node, tid);
      break;

    case TM_SIGNAL:
      break;

    case TM_OBIT:
      obitvalp = (int *)ep->e_info;
      *obitvalp = disrsi(local_conn, &ret);

      if (ret != DIS_SUCCESS)
        {
        DBPRT(("%s: OBIT failed obitval\n", id))
        goto err;
        }

      break;

    case TM_POSTINFO:
      break;

    case TM_GETINFO:
      ihold = (struct infohold *)ep->e_info;
      info = disrcs(local_conn, (size_t *)ihold->info_len, &ret);

      if (ret != DIS_SUCCESS)
        {
        DBPRT(("%s: GETINFO failed info\n", id))
        break;
        }

      memcpy(ihold->info, info, MIN(*ihold->info_len, ihold->len));

      free(info);
      break;

    case TM_RESOURCES:
      rhold = (struct reschold *)ep->e_info;
      info = disrst(local_conn, &ret);

      if (ret != DIS_SUCCESS)
        break;

      strncpy(rhold->resc, info, rhold->len);

      free(info);

      break;

    default:
      DBPRT(("%s: unknown event command %d\n", id, ep->e_mtype))
      goto err;
    }

done:

  del_event(ep);
  return TM_SUCCESS;

err:

  if (ep)
    del_event(ep);

  (void)close(local_conn);

  local_conn = -1;

  return TM_ENOTCONNECTED;
  }



/*
 * tm_adopt() --
 *
 *     When PBS is used in conjuction with an alternative (MPI) task
 *     spawning/management system (AMS) (like Quadrics RMS or SGI array
 *     services), only the script task on the mother superior node will
 *     be parented by (or even known to) a PBS MOM.  Unless the AMS is
 *     PBS-(tm-)aware, all other tasks will be parented (and to varying
 *     extents managed) by the AMS.  This means that PBS cannot track
 *     task resource usage (unless the AMS provides such info) nor
 *     manage (suspend, resume, signal, clean up, ...) the task (unless
 *     the AMS provides such functionality).  For example pvmrun and
 *     some mpiruns simply use rsh to start remote processes - no AMS
 *     tracking or management facilities are available.
 *
 *     This function allows any task (session) to be adopted into a PBS
 *     job. It is used by:
 *         -  "adopter" (which is in turn used by our pvmrun)
 *         -  our rmsloader wrapper (a home-brew replacement for RMS'
 *            rmsloader that does some work and then exec()s the real
 *            rmsloader) to tell PBS to adopt its session id (which
 *            (hopefully) is also the session id for all its child
 *            processes).
 *         -  anumpirun on SGI Altix systems
 *
 *     Call this instead of tm_init() to ask the local pbs_mom to
 *     adopt a session (i.e. create a new task corresponding to the
 *     session id). Note that this may subvert all of the cookie stuff
 *     in PBS as the AMS task starter may not have any PBS cookie info
 *     (eg rmsloader)
 *
 * Arguments:
 *     char *id      AMS altid (eg RMS resource id) or PBS_JOBID
 *                   (depending on adoptCmd) of the job that will adopt
 *                   sid. This is how pbs_mom works out which job will
 *                   adopt the sid.
 *     int adoptCmd  either TM_ADOPT_JOBID or TM_ADOPT_ALTID if task
 *                   id is AMS altid
 *     pid_t pid     process id of process to be adopted (always self?)
 *
 * Assumption:
 *     If TM_ADOPT_ALTID is used to identify tasks to be adopted, PBS
 *     must be configured to work with one and only one alternative task
 *     spawning/management system that uses it own task identifiers.
 *
 * Result:
 *     Returns TM_SUCCESS if the session was successfully adopted by
 *     the mom. Returns TM_ENOTFOUND if the mom couldn't find a job
 *     with the given RMS resource id. Returns TM_ESYSTEM or
 *     TM_ENOTCONNECTED if there was some sort of comms error talking
 *     to the mom
 *
 * Side effects:
 *     Sets the tm_* globals to fake values if tm_init() has never
 *     been called. This mainly just prevents segfaults etc when
 *     these values are written to local_conn - the mom ignores most
 *     of them for this special adopt case
 *
 */

int tm_adopt(char *id, int adoptCmd, pid_t pid)
  {
  int status, ret;
  pid_t sid;
  char *env;

  sid = getsid(pid);

  /* Must be the only call to call to tm and
     must only be called once */

  if (init_done) return TM_BADINIT;

  init_done = 1;

  /* Fabricate the tm state as best we can - not really needed */

  if ((tm_jobid = getenv("PBS_JOBID")) == NULL)
    tm_jobid = "ADOPT JOB";

  tm_jobid_len = strlen(tm_jobid);

  if ((tm_jobcookie = getenv("PBS_JOBCOOKIE")) == NULL)
    tm_jobcookie = "ADOPT COOKIE";

  tm_jobcookie_len = strlen(tm_jobcookie);

  /* We dont have the (right) node id or task id */
  tm_jobndid = 0;

  tm_jobtid = 0;

  /* Fallback is system default MOM port if not known */
  if ((env = getenv("PBS_MOMPORT")) == NULL || (tm_momport = atoi(env)) == 0)
    tm_momport = PBS_MANAGER_SERVICE_PORT;


  /* DJH 27 Feb 2002. two kinds of adoption now */
  if (adoptCmd != TM_ADOPT_ALTID && adoptCmd != TM_ADOPT_JOBID)
    return TM_EUNKNOWNCMD;

  if (startcom(adoptCmd, TM_NULL_EVENT) != DIS_SUCCESS)
    return TM_ESYSTEM;

  /* send session id */
  if (diswsi(local_conn, sid) != DIS_SUCCESS)
    return TM_ENOTCONNECTED;

  /* write the pid so the adopted process can be part of the cpuset if needed */

  if (diswsi(local_conn,sid) != DIS_SUCCESS)
    return TM_ENOTCONNECTED;

  /* send job or alternative id */
  if (diswcs(local_conn, id, strlen(id)) != DIS_SUCCESS)
    return TM_ENOTCONNECTED;

  DIS_tcp_wflush(local_conn);

  /* The mom should now attempt to adopt the task and will send back a
     status flag to indicate whether it was successful or not. */

  status = disrsi(local_conn, &ret);

  if (ret != DIS_SUCCESS)
    return TM_ENOTCONNECTED;

  /* Don't allow any more tm_* calls in this process. As well as
     closing an unused socket it also prevents any problems related to
     the fact that all adopted processes have a fake task id which
     might break the tm mechanism */
  tm_finalize();

  /* Since we're not using events, tm_finalize won't actually
     close the socket, so do it here. */
  if (local_conn > -1)
    {
    close(local_conn);
    local_conn = -1;
    }

  return (status == TM_OKAY ?

          TM_SUCCESS :
          TM_ENOTFOUND);
  }
