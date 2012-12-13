#include "license_pbs.h" /* See here for the software license */
/*
 * This file contains the routines used to send a reply to a client following
 * the processing of a request.  The following routines are provided here:
 *
 * reply_send()  - the main routine, used by all reply senders
 * reply_ack()   - send a basic no error acknowledgement
 * req_reject()  - send a basic error return
 * reply_text()  - send a return with a supplied text string
 * reply_jobid() - used by several requests where the job id must be sent
 * reply_free()  - free the substructure that might hang from a reply
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "reply_send.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "libpbs.h"
#include "dis.h"
#include "log.h"
#include "../lib/Liblog/pbs_log.h"
#include "../lib/Liblog/log_event.h"
#include "pbs_error.h"
#include "server_limits.h"
#include "list_link.h"
#include "net_connect.h"
#include "attribute.h"
#include "credential.h"
#include "batch_request.h"
#include "work_task.h"
#include "utils.h"
#include "tcp.h" /* tcp_chan */




/* External Globals */
extern char *msg_daemonname;

#ifndef PBS_MOM
extern all_tasks task_list_event;
extern int LOGLEVEL;
#endif /* PBS_MOM */

#define ERR_MSG_SIZE 127


static void set_err_msg(

  int   code,
  char *msgbuf,
  int   len)

  {
  char *msg = NULL;
  char *msg_tmp;

  /* see if there is an error message associated with the code */

  *msgbuf = '\0';

  if (code == PBSE_SYSTEM)
    {
    snprintf(msgbuf, len, "%s%s", msg_daemonname, pbse_to_txt(PBSE_SYSTEM));

    msg_tmp = strerror(errno);

    if (msg_tmp)
      safe_strncat(msgbuf, strerror(errno), len - strlen(msgbuf));
    else
      safe_strncat(msgbuf, "Unknown error", len - strlen(msgbuf));
    }
  else if (code > PBSE_)
    {
    msg = pbse_to_txt(code);
    }
  else
    {
    msg = strerror(code);
    }

  if (msg)
    {
    snprintf(msgbuf, len, "%s", msg);
    }

  return;
  }  /* END set_err_msg() */






static int dis_reply_write(

  int                 sfds,    /* I */
  struct batch_reply *preply)  /* I */

  {
  int              rc = PBSE_NONE;
  char             log_buf[LOCAL_LOG_BUF_SIZE];
  struct tcp_chan *chan = NULL;

  /* setup for DIS over tcp */
  if ((chan = DIS_tcp_setup(sfds)) == NULL)
    {
    }

  /* send message to remote client */
  else if ((rc = encode_DIS_reply(chan, preply)) ||
           (rc = DIS_tcp_wflush(chan)))
    {
    sprintf(log_buf, "DIS reply failure, %d", rc);

    log_event(PBSEVENT_SYSTEM, PBS_EVENTCLASS_REQUEST, __func__, log_buf);

    /* don't need to get the lock here because we already have it from process request */
    close_conn(sfds, FALSE);
    }

  if (chan != NULL)
    DIS_tcp_cleanup(chan);

  return(rc);
  }  /* END dis_reply_write() */




/*
 * reply_send - Send a reply to a batch request, reply either goes to
 * remote client over the network:
 * Encode the reply to a "presentation element",
 * allocate the presenetation stream and attach to socket,
 * write out reply, and free ps, pe, and isoreply structures.
 *
 * Or the reply is for a request from the local server:
 * locate the work task associated with the request and dispatch it
 *
 * The request (and reply) structures are freed.
 * There are now two functions that do this.
 * reply_send_svr and reply_send_mom
 * reply_send is kept for compatibility purposes
 */

int reply_send(

  struct batch_request *request)  /* I (freed) */

  {
#ifdef PBS_MOM
  return(reply_send_mom(request));
#else
  return(reply_send_svr(request));
#endif
  }

#ifndef PBS_MOM
int reply_send_svr(
  
  struct batch_request *request)  /* I (freed) */

  {
  int               rc = 0;
  char              log_buf[LOCAL_LOG_BUF_SIZE];
  int               sfds = request->rq_conn;  /* socket */

  /* Handle remote replies - local batch requests no longer create work tasks */
  if (sfds >= 0)
    {
    /* Otherwise, the reply is to be sent to a remote client */

    if (request->rq_noreply != TRUE)
      {
      rc = dis_reply_write(sfds, &request->rq_reply);

      if (LOGLEVEL >= 7)
        {
        sprintf(log_buf, "Reply sent for request type %s on socket %d",
          reqtype_to_txt(request->rq_type),
          sfds);

        log_record(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, __func__, log_buf);
        }
      }
    }

  if (((request->rq_type != PBS_BATCH_AsyModifyJob) && 
       (request->rq_type != PBS_BATCH_AsyrunJob) &&
       (request->rq_type != PBS_BATCH_AsySignalJob)) ||
      (request->rq_noreply == TRUE))
    {
    free_br(request);
    }

  return(rc);
  }  /* END reply_send_svr() */
#endif /* PBS_MOM */




int reply_send_mom(

  struct batch_request *request)  /* I (freed) */

  {
  int      rc = 0;
  int      sfds = request->rq_conn;  /* socket */

  /* determine where the reply should go, remote or local */

  if (sfds == PBS_LOCAL_CONNECTION)
    {
    rc = PBSE_SYSTEM;
    }
  else if (sfds >= 0)
    {
    /* Otherwise, the reply is to be sent to a remote client */
    rc = dis_reply_write(sfds, &request->rq_reply);
    }
  free_br(request);
  return(rc);
  }  /* END reply_send_mom() */





/*
 * reply_ack - Send a normal acknowledgement reply to a request
 *
 * Always frees the request structure.
 */

void reply_ack(
    
  struct batch_request *preq)

  {

  preq->rq_reply.brp_code    = PBSE_NONE;
  preq->rq_reply.brp_auxcode = 0;
  preq->rq_reply.brp_choice  = BATCH_REPLY_CHOICE_NULL;
  reply_send(preq);
  }

/*
 * reply_free - free any sub-struttures that might hang from the basic
 * batch_reply structure, the reply structure itself IS NOT FREED.
 */

void reply_free(

  struct batch_reply *prep)

  {

  struct brp_status  *pstat;

  struct brp_status  *pstatx;

  struct brp_select  *psel;

  struct brp_select  *pselx;

  if (prep->brp_choice == BATCH_REPLY_CHOICE_Text)
    {
    if (prep->brp_un.brp_txt.brp_str)
      {
      (void)free(prep->brp_un.brp_txt.brp_str);
      prep->brp_un.brp_txt.brp_str = NULL;
      prep->brp_un.brp_txt.brp_txtlen = 0;
      }

    }
  else if (prep->brp_choice == BATCH_REPLY_CHOICE_Select)
    {
    psel = prep->brp_un.brp_select;

    while (psel)
      {
      pselx = psel->brp_next;
      (void)free(psel);
      psel = pselx;
      }

    }
  else if (prep->brp_choice == BATCH_REPLY_CHOICE_Status)
    {
    pstat = (struct brp_status *)GET_NEXT(prep->brp_un.brp_status);

    while (pstat)
      {
      pstatx = (struct brp_status *)GET_NEXT(pstat->brp_stlink);
      free_attrlist(&pstat->brp_attr);
      (void)free(pstat);
      pstat = pstatx;
      }
    }
  else if (prep->brp_choice == BATCH_REPLY_CHOICE_RescQuery)
    {
    (void)free(prep->brp_un.brp_rescq.brq_avail);
    (void)free(prep->brp_un.brp_rescq.brq_alloc);
    (void)free(prep->brp_un.brp_rescq.brq_resvd);
    (void)free(prep->brp_un.brp_rescq.brq_down);
    }

  prep->brp_choice = BATCH_REPLY_CHOICE_NULL;

  return;
  }  /* END reply_free() */





/*
 * req_reject - create a reject (error) reply for a request and send it
 *
 * Always frees the request structure.
 */

void req_reject(

  int                   code,      /* I */
  int                   aux,       /* I */
  struct batch_request *preq,      /* I */
  const char          *HostName,  /* I (optional) */
  const char          *Msg)       /* I (optional) */

  {
  char msgbuf[ERR_MSG_SIZE + 256 + 1];
  char msgbuf2[ERR_MSG_SIZE + 256 + 1];
  char log_buf[LOCAL_LOG_BUF_SIZE];

  set_err_msg(code, msgbuf, sizeof(msgbuf));

  snprintf(msgbuf2, sizeof(msgbuf2), "%s", msgbuf);

  if ((HostName != NULL) && (*HostName != '\0'))
    {
    snprintf(msgbuf, sizeof(msgbuf), "%s REJHOST=%s",
      msgbuf2,
      HostName);
    
    snprintf(msgbuf2, sizeof(msgbuf2), "%s", msgbuf);
    }

  if ((Msg != NULL) && (*Msg != '\0'))
    {
    snprintf(msgbuf, sizeof(msgbuf), "%s MSG=%s",
      msgbuf2,
      Msg);

    /* NOTE: Don't need this last snprintf() unless another message is concatenated. */
    }

  sprintf(log_buf, "Reject reply code=%d(%s), aux=%d, type=%s, from %s@%s",
    code,
    msgbuf,
    aux,
    reqtype_to_txt(preq->rq_type),
    preq->rq_user,
    preq->rq_host);

  log_event(PBSEVENT_DEBUG,PBS_EVENTCLASS_REQUEST,"req_reject",log_buf);

  preq->rq_reply.brp_auxcode = aux;

  reply_text(preq, code, msgbuf);

  return;
  }  /* END req_reject() */




/*
 * reply_badattr - create a reject (error) reply for a request including
 * the name of the bad attribute/resource
 */

void reply_badattr(

  int                   code,
  int                   aux,
  svrattrl        *pal,
  struct batch_request *preq)

  {
  int   i = 1;
  char  msgbuf[ERR_MSG_SIZE+1];

  set_err_msg(code, msgbuf, sizeof(msgbuf));

  while (pal)
    {
    if (i == aux)
      {
      strcat(msgbuf, " ");
      strcat(msgbuf, pal->al_name);

      if (pal->al_resc)
        {
        strcat(msgbuf, ".");
        strcat(msgbuf, pal->al_resc);
        }

      break;
      }

    pal = (svrattrl *)GET_NEXT(pal->al_link);

    ++i;
    }

  reply_text(preq, code, msgbuf);

  return;
  }  /* END reply_badattr() */




/*
 * reply_text - return a reply with a supplied text string
 */

void reply_text(

  struct batch_request *preq,
  int                   code,
  char                 *text) /* I */

  {
  if (preq->rq_reply.brp_choice != BATCH_REPLY_CHOICE_NULL)
    {
    /* in case another reply was being built up, clean it out */

    reply_free(&preq->rq_reply);
    }

  preq->rq_reply.brp_code    = code;

  preq->rq_reply.brp_auxcode = 0;

  if ((text != NULL) && (text[0] != '\0'))
    {
    preq->rq_reply.brp_choice                = BATCH_REPLY_CHOICE_Text;
    preq->rq_reply.brp_un.brp_txt.brp_str    = strdup(text);
    preq->rq_reply.brp_un.brp_txt.brp_txtlen = strlen(text);
    }
  else
    {
    preq->rq_reply.brp_choice = BATCH_REPLY_CHOICE_NULL;
    }

  reply_send(preq);

  return;
  }  /* END reply_text() */




/*
 * reply_jobid - return a reply with the job id, used by
 * req_queuejob(), req_rdytocommit(), and req_commit()
 *
 * Always frees the request structure.
 */

int reply_jobid(

  struct batch_request *preq,
  char   *jobid,
  int    which)

  {
  preq->rq_reply.brp_code    = 0;
  preq->rq_reply.brp_auxcode = 0;
  preq->rq_reply.brp_choice  = which;

  snprintf(preq->rq_reply.brp_un.brp_jid, sizeof(preq->rq_reply.brp_un.brp_jid), "%s", jobid);

  return(reply_send(preq));
  }  /* END reply_jobid() */

