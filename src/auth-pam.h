/*!
 * node-auth-pam - auth-pam.h
 * Copyright(c) 2020 Marian Kapisinsky
 * MIT Licensed
 */

#include <stdbool.h>
#include <pthread.h>

#include <node_api.h>

#define BUFFERSIZE 256

#define NODE_PAM_JS_CONV 50
#define NODE_PAM_ERR 51

typedef struct {
  char *service;
  char *username;
  char *message;
  int msgStyle;
  char *response;
  bool respFlag;
  int retval;
  pthread_t thread;
  pthread_mutex_t mutex;
  napi_threadsafe_function tsfn;
} nodepamCtx;

void nodepamAuthenticate(nodepamCtx *ctx);

void nodepamSetResponse(nodepamCtx *ctx, const char *response, size_t responseSize);

void nodepamCleanup(nodepamCtx *ctx);

void nodepamTerminate(nodepamCtx *ctx);
