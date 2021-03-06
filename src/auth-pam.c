/**
 * node-auth-pam - auth-pam.c
 * Copyright(c) 2020 Marian Kapisinsky
 * MIT Licensed
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>

#include <security/pam_appl.h>

#include <node_api.h>

#include "auth-pam.h"

/**
 * Prepare the current PAM message to nodepamCtx,
 * set response to NULL, respFlag to false and retval to NODE_PAM_JS_CONV.
*/

void prepareMessage(nodepamCtx *ctx, int msg_style, const char *msg) {

  ctx->msgStyle = msg_style;

  ctx->message = memset(malloc((strlen(msg))), 0, (strlen(msg)));
  ctx->message = strdup(msg);

  ctx->response = NULL;
  ctx->respFlag = false;
  ctx->retval = NODE_PAM_JS_CONV;
}

/**
 * Call the N-API thread-safe function to pass the message to Node.js
 * (this calls the authenticate() bindings's callback).
 */

void sendMessage(nodepamCtx *ctx) {

  assert(napi_call_threadsafe_function(ctx->tsfn, ctx, napi_tsfn_blocking) == napi_ok);
}

/**
 * Addon's conversation function.
*/

int nodepamConv( int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr ) {

  if ( !msg || !resp || !appdata_ptr ) return PAM_CONV_ERR;

  if ( num_msg <= 0 || num_msg > PAM_MAX_NUM_MSG ) return PAM_CONV_ERR;

  struct pam_response *response = NULL;
  response = memset(malloc(num_msg * sizeof(struct pam_response)), 0, num_msg * sizeof(struct pam_response));
  if ( !response ) return PAM_CONV_ERR;

  nodepamCtx *ctx = (nodepamCtx *)appdata_ptr;

  for ( int i = 0; i < num_msg; i++ ) {

    switch (msg[i]->msg_style) {

      case PAM_PROMPT_ECHO_OFF:
      case PAM_PROMPT_ECHO_ON:

        prepareMessage(ctx, msg[i]->msg_style, msg[i]->msg);

        // Pass the message to Node.js
        sendMessage(ctx);

        // Wait for response
        while(true) {

          pthread_mutex_lock(&(ctx->mutex));
          if (!ctx->respFlag) {
            pthread_mutex_unlock(&(ctx->mutex));
            continue;
          } 
          else {
            response[i].resp = strdup(ctx->response);
            response[i].resp_retcode = 0;
            pthread_mutex_unlock(&(ctx->mutex));
            break;
          }
        }	

        free(ctx->message);
        free(ctx->response);
        break;

      case PAM_ERROR_MSG:
      case PAM_TEXT_INFO:
        
        prepareMessage(ctx, msg[i]->msg_style, msg[i]->msg);
	
        // Pass the message into JavaScript
        sendMessage(ctx);

        // Wait for response - just for synchronization purposes, no response is set
        while(true) {

          pthread_mutex_lock(&(ctx->mutex));
          if (!ctx->respFlag) {
                  pthread_mutex_unlock(&(ctx->mutex));
                  continue;
                }
          else {
                  pthread_mutex_unlock(&(ctx->mutex));
            break;
          }	
        }

	      free(ctx->message);
        break;
        
      default:
        return PAM_CONV_ERR;
    }
  }

  *resp = response;
  return PAM_SUCCESS;
}

void nodepamAuthenticate(nodepamCtx *ctx) {

  pam_handle_t *pamh = NULL;
  int retval = 0;

  struct pam_conv conv = { &nodepamConv, (void *) ctx };

  retval = pam_start(ctx->service, ctx->username, &conv, &pamh);
  
  if( retval == PAM_SUCCESS )
    retval = pam_authenticate(pamh, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);

  pam_end(pamh, retval);

  ctx->retval = retval;
}

void nodepamSetResponse(nodepamCtx *ctx, const char *response, size_t responseSize) {

  pthread_mutex_lock(&(ctx->mutex));

  ctx->response = memset(malloc(responseSize), 0, responseSize);

  if (!ctx->response) {
    ctx->retval = NODE_PAM_ERR;
    assert(napi_release_threadsafe_function(ctx->tsfn, napi_tsfn_release) == napi_ok);
    return;
  }

  if (ctx->msgStyle == PAM_PROMPT_ECHO_OFF || ctx->msgStyle == PAM_PROMPT_ECHO_ON ) 
  	ctx->response = strdup(response);

  ctx->respFlag = true;
  pthread_mutex_unlock(&(ctx->mutex));
}

void nodepamCleanup(nodepamCtx *ctx) {

  if (ctx->retval == NODE_PAM_ERR) {
    kill(ctx->thread, SIGTERM);
    free(ctx->message);
  } else {
    assert(pthread_join(ctx->thread, NULL) == 0);
  }
  
  pthread_mutex_destroy(&(ctx->mutex));

  ctx->tsfn = NULL;

  free(ctx->service);
  free(ctx->username);
  free(ctx);
}

void nodepamKill(nodepamCtx *ctx) {

  ctx->retval = NODE_PAM_ERR;
}
