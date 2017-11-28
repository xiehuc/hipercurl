/*
 * hiperfifo.h
 * Copyright (C) 2017 xiehuc <xiehucheng@baidu.com>
 *
 * Distributed under terms of the BAIDU license.
 */

#ifndef HIPERFIFO_H
#define HIPERFIFO_H

#include <curl/curl.h>
#include <ev.h>
#include "queue.h"

/* Global information, common to all connections */
typedef struct _GlobalInfo
{
  struct ev_loop *loop;
  struct ev_io fifo_event;
  struct ev_timer timer_event;
  CURLM *multi;
  int still_running;
  int max_running; // const set by params
  FILE *input;
  char* linebuffer;
  size_t capacity;
  struct curl_slist* header;
  int (*start_io)(struct _GlobalInfo *g);
  TAILQ_HEAD(, _ConnInfo) infohead;
} GlobalInfo;

struct string_list {
  char *data;
  size_t size;
  TAILQ_ENTRY(string_list) entries;
};

/* Information associated with a specific easy handle */
typedef struct _ConnInfo
{
  CURL *easy; // == NULL means done
  char *url;
  GlobalInfo *global;
  char error[CURL_ERROR_SIZE];

  TAILQ_HEAD(, string_list) body;
  TAILQ_ENTRY(_ConnInfo) entries;
} ConnInfo;

void new_conn(char *url, char* post, GlobalInfo *g);
void init_global(GlobalInfo* g);
void direct_output(char* line, size_t size, GlobalInfo* g);

#endif /* !HIPERFIFO_H */
