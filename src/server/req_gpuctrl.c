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
/*
 * req_track.c
 *
 * Functions relation to the Track Job Request and job tracking.
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "libpbs.h"
#include <signal.h>
#include "attribute.h"
#include "server.h"
#include "net_connect.h"
#include "batch_request.h"
#include "pbs_nodes.h"
#include "pbs_error.h"
#include "work_task.h"
#include "svrfunc.h"
#include "log.h"
#include "../lib/Liblog/pbs_log.h"
#include "pbs_nodes.h"
#include "svr_connect.h" /* svr_connect, svr_disconnect_sock */

/* External Functions */

extern int gpu_entry_by_id(struct pbsnode *,const char *, int);

/* Private Functions Local to this file */

static void process_gpu_request_reply(batch_request *preq);

/* Global Data Items: */

extern int           LOGLEVEL;
extern unsigned int  pbs_mom_port;
/*
 * req_gpuctrl_svr - Do a GPU change mode
 */

int req_gpuctrl_svr(
    
  struct batch_request *preq)

  {
  int rc = PBSE_NONE;
  char  *nodename = NULL;
  char  *gpuid = NULL;
  int    gpumode = -1;
  int    reset_perm = -1;
  int    reset_vol = -1;
  char   log_buf[LOCAL_LOG_BUF_SIZE+1];
  int    local_errno = 0;
  struct pbsnode *pnode = NULL;
  int    gpuidx = -1;
  int    conn;

  if ((preq->rq_perm &
       (ATR_DFLAG_MGWR | ATR_DFLAG_MGRD | ATR_DFLAG_OPRD | ATR_DFLAG_OPWR)) == 0)
    {
    rc = PBSE_PERM;
    snprintf(log_buf, LOCAL_LOG_BUF_SIZE,
        "invalid permissions (ATR_DFLAG_MGWR | ATR_DFLAG_MGRD | ATR_DFLAG_OPRD | ATR_DFLAG_OPWR)");
    req_reject(rc, 0, preq, NULL, log_buf);
    return rc;
    }

  nodename = preq->rq_ind.rq_gpuctrl.rq_momnode;
  gpuid = preq->rq_ind.rq_gpuctrl.rq_gpuid;
  gpumode = preq->rq_ind.rq_gpuctrl.rq_gpumode;
  reset_perm = preq->rq_ind.rq_gpuctrl.rq_reset_perm;
  reset_vol = preq->rq_ind.rq_gpuctrl.rq_reset_vol;

  if (LOGLEVEL >= 7)
    {
    sprintf(
      log_buf,
      "GPU control request for node %s gpuid %s mode %d reset_perm %d reset_vol %d",
      nodename,
      gpuid,
      gpumode,
      reset_perm,
      reset_vol);

    log_ext(-1, __func__, log_buf, LOG_INFO);
    }

  /* validate mom node exists */

  pnode = find_nodebyname(nodename);

  if (pnode == NULL)
    {
    req_reject(PBSE_UNKNODE, 0, preq, NULL, NULL);
    return PBSE_UNKNODE;
    }

  /* validate that the node is up */

  if ((pnode->nd_state & (INUSE_NOT_READY | INUSE_OFFLINE | INUSE_UNKNOWN))||(pnode->nd_power_state != POWER_STATE_RUNNING))
    {
    rc = PBSE_UNKREQ;
    sprintf(log_buf, "Node %s is not available", pnode->get_name());
    req_reject(rc, 0, preq, NULL, log_buf);
    pnode->unlock_node(__func__, NULL, LOGLEVEL);
    return rc;
    }

  /* validate that the node has real gpus not virtual */

  if (!pnode->nd_gpus_real)
    {
    rc = PBSE_UNKREQ;
    req_reject(rc, 0, preq, NULL, "Not allowed for virtual gpus");
    pnode->unlock_node(__func__, NULL, LOGLEVEL);
    return rc;
    }

  /* validate the gpuid exists */

  if ((gpuidx = gpu_entry_by_id(pnode, gpuid, FALSE)) == -1)
    {
    rc = PBSE_UNKREQ;
    req_reject(rc, 0, preq, NULL, "GPU ID does not exist on node");
    pnode->unlock_node(__func__, NULL, LOGLEVEL);
    return rc;
    }

  /* validate that we have a real request */

  if ((gpumode == -1) && (reset_perm == -1) && (reset_vol == -1))
    {
    rc = PBSE_UNKREQ;
    req_reject(rc, 0, preq, NULL, "No action specified");
    pnode->unlock_node(__func__, NULL, LOGLEVEL);
    return rc;
    }

  /* for mode changes validate the mode with the driver_version */

  if ((pnode->nd_gpusn[gpuidx].driver_ver == 260) && (gpumode > 2))
    {
    rc = PBSE_UNKREQ;
    req_reject(rc, 0, preq, NULL, "GPU driver version does not support mode 3");
    pnode->unlock_node(__func__, NULL, LOGLEVEL);
    return rc;
    }

  /* we need to relay request to the mom for processing */
  /* have MOM attempt to change the gpu mode */

  preq->rq_orgconn = preq->rq_conn;  /* restore client socket */

  pnode->unlock_node(__func__, NULL, LOGLEVEL);
  conn = svr_connect(
           pnode->nd_addrs[0],
           pbs_mom_port,
           &local_errno,
           NULL,
           NULL);
    

  if (conn >= 0)
    {
    if ((rc = issue_Drequest(conn, preq, true)) != PBSE_NONE)
      req_reject(rc, 0, preq, NULL, NULL);
    else
      process_gpu_request_reply(preq);
    }
  else
    {
    req_reject(PBSE_UNKREQ, 0, preq, NULL, "Failed to get connection to mom");
    }

  return rc;
  }



/*
 * process_gpu_request_reply
 * called when a gpu change request was sent to MOM and the answer
 * is received.  Completes the gpu request.
 */
void process_gpu_request_reply(

  batch_request *preq)

  {
  char log_buf[LOCAL_LOG_BUF_SIZE];

  if (preq == NULL)
    return;

  preq->rq_conn = preq->rq_orgconn;  /* restore client socket */

  if (preq->rq_reply.brp_code != 0)
    {
    sprintf(log_buf,
      "MOM failed on GPU request, rc = %d",
      preq->rq_reply.brp_code);
    log_err(errno, __func__, log_buf);

    req_reject(preq->rq_reply.brp_code, 0, preq, NULL, log_buf);
    }
  else
    {
    /* record that MOM changed gpu mode */
    if (LOGLEVEL >= 7)
      {
      sprintf(
        log_buf,
        "GPU control request completed for node %s gpuid %s mode %d reset_perm %d reset_vol %d",
        preq->rq_ind.rq_gpuctrl.rq_momnode,
        preq->rq_ind.rq_gpuctrl.rq_gpuid,
        preq->rq_ind.rq_gpuctrl.rq_gpumode,
        preq->rq_ind.rq_gpuctrl.rq_reset_perm,
        preq->rq_ind.rq_gpuctrl.rq_reset_vol);

      log_ext(-1, __func__, log_buf, LOG_INFO);
      }

    reply_ack(preq);
    }
  } /* END process_gpu_request_reply() */
