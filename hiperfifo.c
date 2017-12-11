/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2017, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
/* <DESC>
 * multi socket interface together with libev
 * </DESC>
 */
/* Example application source code using the multi socket interface to
 * download many files at once.
 *
 * This example features the same basic functionality as hiperfifo.c does,
 * but this uses libev instead of libevent.
 *
 * Written by Jeff Pohlmeyer, converted to use libev by Markus Koetter

Requires libev and a (POSIX?) system that has mkfifo().

This is an adaptation of libcurl's "hipev.c" and libevent's "event-test.c"
sample programs.

When running, the program creates the named pipe "hiper.fifo"

Whenever there is input into the fifo, the program reads the input as a list
of URL's and creates some new easy handles to fetch each URL via the
curl_multi "hiper" API.


Thus, you can try a single URL:
  % echo http://www.yahoo.com > hiper.fifo

Or a whole bunch of them:
  % cat my-url-list > hiper.fifo

The fifo buffer is handled almost instantly, so you can even add more URL's
while the previous requests are still being downloaded.

Note:
  For the sake of simplicity, URL length is limited to 1023 char's !

This is purely a demo app, all retrieved data is simply discarded by the write
callback.

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "hiperfifo.h"

//#define DPRINT(x...) printf(x)
#define DPRINT(x...)

/* Information associated with a specific socket */
typedef struct _SockInfo
{
  curl_socket_t sockfd;
  CURL *easy;
  int action;
  long timeout;
  struct ev_io ev;
  int evset;
  GlobalInfo *global;
} SockInfo;

char delimiter = '\n';
static void timer_cb(EV_P_ struct ev_timer *w, int revents);

/* Update the event timer after curl_multi library calls */
static int multi_timer_cb(CURLM *multi, long timeout_ms, GlobalInfo *g)
{
  DPRINT("%s %li\n", __PRETTY_FUNCTION__,  timeout_ms);
  ev_timer_stop(g->loop, &g->timer_event);
  if(timeout_ms > 0) {
    double  t = timeout_ms / 1000;
    ev_timer_init(&g->timer_event, timer_cb, t, 0.);
    ev_timer_start(g->loop, &g->timer_event);
  }
  else if(timeout_ms == 0)
    timer_cb(g->loop, &g->timer_event, 0);
  return 0;
}

/* Die if we get a bad CURLMcode somewhere */
static void mcode_or_die(const char *where, CURLMcode code)
{
  if(CURLM_OK != code) {
    const char *s;
    switch(code) {
    case CURLM_BAD_HANDLE:
      s = "CURLM_BAD_HANDLE";
      break;
    case CURLM_BAD_EASY_HANDLE:
      s = "CURLM_BAD_EASY_HANDLE";
      break;
    case CURLM_OUT_OF_MEMORY:
      s = "CURLM_OUT_OF_MEMORY";
      break;
    case CURLM_INTERNAL_ERROR:
      s = "CURLM_INTERNAL_ERROR";
      break;
    case CURLM_UNKNOWN_OPTION:
      s = "CURLM_UNKNOWN_OPTION";
      break;
    case CURLM_LAST:
      s = "CURLM_LAST";
      break;
    default:
      s = "CURLM_unknown";
      break;
    case CURLM_BAD_SOCKET:
      s = "CURLM_BAD_SOCKET";
      DPRINT( "ERROR: %s returns %s\n", where, s);
      /* ignore this error */
      return;
    }
    DPRINT( "ERROR: %s returns %s\n", where, s);
    exit(code);
  }
}

static void free_conn(ConnInfo* info) {
  if (info->url) {
    free(info->url);
  }
  free(info);
}

static int queue_output(GlobalInfo* g)
{
  ConnInfo* info;
  do {
    info = TAILQ_FIRST(&g->infohead);
    if (!info || info->easy) {
      break;
    }
    TAILQ_REMOVE(&g->infohead, info, entries);
    struct string_list* piece, *tpiece;
    TAILQ_FOREACH_SAFE(piece, &info->body, entries, tpiece) {
      fwrite(piece->data, piece->size, 1, stdout);
      fwrite(&delimiter, 1, 1, stdout);
      TAILQ_REMOVE(&info->body, piece, entries);
      free(piece->data);
      free(piece);
    }
    free_conn(info);
  } while(1);
  if (g->still_running <= g->max_running) {
    g->start_io(g);
  }
  return 0;
}

/* Check for completed transfers, and remove their easy handles */
static void check_multi_info(GlobalInfo *g)
{
  char *eff_url;
  CURLMsg *msg;
  int msgs_left;
  ConnInfo *conn;
  CURL *easy;
  CURLcode res;

  while((msg = curl_multi_info_read(g->multi, &msgs_left))) {
    if(msg->msg == CURLMSG_DONE) {
      easy = msg->easy_handle;
      res = msg->data.result;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
      DPRINT( "DONE: %s => (%d) %s\n", eff_url, res, conn->error);
      curl_multi_remove_handle(g->multi, easy);
      curl_easy_cleanup(easy);
      conn->easy = NULL;
      queue_output(g);
    }
  }
}



/* Called by libevent when we get action on a multi socket */
static void event_cb(EV_P_ struct ev_io *w, int revents)
{
  DPRINT("%s  w %p revents %i\n", __PRETTY_FUNCTION__, w, revents);
  GlobalInfo *g = (GlobalInfo*) w->data;
  CURLMcode rc;

  int action = (revents&EV_READ?CURL_POLL_IN:0)|
    (revents&EV_WRITE?CURL_POLL_OUT:0);
  rc = curl_multi_socket_action(g->multi, w->fd, action, &g->still_running);
  mcode_or_die("event_cb: curl_multi_socket_action", rc);
  check_multi_info(g);
  if(g->still_running <= 0) {
    DPRINT( "last transfer done, kill timeout\n");
    ev_timer_stop(g->loop, &g->timer_event);
  }
}

/* Called by libevent when our timeout expires */
static void timer_cb(EV_P_ struct ev_timer *w, int revents)
{
  DPRINT("%s  w %p revents %i\n", __PRETTY_FUNCTION__, w, revents);

  GlobalInfo *g = (GlobalInfo *)w->data;
  CURLMcode rc;

  rc = curl_multi_socket_action(g->multi, CURL_SOCKET_TIMEOUT, 0,
                                &g->still_running);
  mcode_or_die("timer_cb: curl_multi_socket_action", rc);
  check_multi_info(g);
}

/* Clean up the SockInfo structure */
static void remsock(SockInfo *f, GlobalInfo *g)
{
  DPRINT("%s  \n", __PRETTY_FUNCTION__);
  if(f) {
    if(f->evset)
      ev_io_stop(g->loop, &f->ev);
    free(f);
  }
}



/* Assign information to a SockInfo structure */
static void setsock(SockInfo *f, curl_socket_t s, CURL *e, int act,
                    GlobalInfo *g)
{
  DPRINT("%s  \n", __PRETTY_FUNCTION__);

  int kind = (act&CURL_POLL_IN?EV_READ:0)|(act&CURL_POLL_OUT?EV_WRITE:0);

  f->sockfd = s;
  f->action = act;
  f->easy = e;
  if(f->evset)
    ev_io_stop(g->loop, &f->ev);
  ev_io_init(&f->ev, event_cb, f->sockfd, kind);
  f->ev.data = g;
  f->evset = 1;
  ev_io_start(g->loop, &f->ev);
}



/* Initialize a new SockInfo structure */
static void addsock(curl_socket_t s, CURL *easy, int action, GlobalInfo *g)
{
  SockInfo *fdp = calloc(sizeof(SockInfo), 1);

  fdp->global = g;
  setsock(fdp, s, easy, action, g);
  curl_multi_assign(g->multi, s, fdp);
}

/* CURLMOPT_SOCKETFUNCTION */
static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
  DPRINT("%s e %p s %i what %i cbp %p sockp %p\n",
         __PRETTY_FUNCTION__, e, s, what, cbp, sockp);

  GlobalInfo *g = (GlobalInfo*) cbp;
  SockInfo *fdp = (SockInfo*) sockp;
  const char *whatstr[]={ "none", "IN", "OUT", "INOUT", "REMOVE"};
  (void)whatstr;

  DPRINT("socket callback: s=%d e=%p what=%s ", s, e, whatstr[what]);
  if(what == CURL_POLL_REMOVE) {
    DPRINT( "\n");
    remsock(fdp, g);
  }
  else {
    if(!fdp) {
      DPRINT( "Adding data: %s\n", whatstr[what]);
      addsock(s, e, what, g);
    }
    else {
      DPRINT(
              "Changing action from %s to %s\n",
              whatstr[fdp->action], whatstr[what]);
      setsock(fdp, s, e, what, g);
    }
  }
  return 0;
}


/* CURLOPT_WRITEFUNCTION */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  size_t realsize = size * nmemb;
  ConnInfo *conn = (ConnInfo*) data;
  struct string_list* list = malloc(sizeof(*list));
  list->data = strdup(ptr);
  list->size = realsize;
  TAILQ_INSERT_TAIL(&conn->body, list, entries);
  return realsize;
}


/* CURLOPT_PROGRESSFUNCTION */
static int prog_cb(void *p, double dltotal, double dlnow, double ult,
                   double uln)
{
  ConnInfo *conn = (ConnInfo *)p;
  (void)ult;
  (void)uln;
  (void)conn;

  DPRINT( "Progress: %s (%g/%g)\n", conn->url, dlnow, dltotal);
  return 0;
}

void direct_output(char* line, size_t size, GlobalInfo* g) {
  ConnInfo* conn = malloc(sizeof(*conn));
  TAILQ_INIT(&conn->body);
  conn->global = g;
  struct string_list* list = malloc(sizeof(*list));
  list->data = strdup(line);
  list->size = size;
  if (line[size-1] == '\n') {
    list->size --;
  }
  TAILQ_INSERT_TAIL(&conn->body, list, entries);
  TAILQ_INSERT_TAIL(&g->infohead, conn, entries);
}

/* Create a new easy handle, and add it to the global curl_multi */
void new_conn(char *url, char* post, GlobalInfo *g)
{
  ConnInfo *conn;
  CURLMcode rc;

  conn = calloc(1, sizeof(ConnInfo));
  memset(conn, 0, sizeof(ConnInfo));
  conn->error[0]='\0';
  TAILQ_INIT(&conn->body);

  conn->easy = curl_easy_init();
  if(!conn->easy) {
    DPRINT( "curl_easy_init() failed, exiting!\n");
    exit(2);
  }
  conn->global = g;
  size_t len = strlen(url);
  if (url[len-1] == '\n') --len;
  conn->url = strndup(url, len);
  curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
  curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
  curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
  curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(conn->easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
  curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
  curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 3L);
  curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
  if (post) {
    curl_easy_setopt(conn->easy, CURLOPT_POST, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_COPYPOSTFIELDS, post);
    curl_easy_setopt(conn->easy, CURLOPT_HTTPHEADER, g->header);
  }

  DPRINT(
          "Adding easy %p to multi %p (%s)\n", conn->easy, g->multi, url);
  rc = curl_multi_add_handle(g->multi, conn->easy);
  mcode_or_die("new_conn: curl_multi_add_handle", rc);

  TAILQ_INSERT_TAIL(&g->infohead, conn, entries);
  /* note that the add_handle() will set a time-out to trigger very soon so
     that the necessary socket_action() call will be called by this app */
}

void init_global(GlobalInfo* g)
{
  memset(g, 0, sizeof(GlobalInfo));
  g->loop = ev_default_loop(0);
  TAILQ_INIT(&g->infohead);
  curl_slist_append(g->header, "Expect: ");
  curl_slist_append(g->header, "Content-Type: application/json");
  g->multi = curl_multi_init();

  ev_timer_init(&g->timer_event, timer_cb, 0., 0.);
  g->timer_event.data = g;
  curl_multi_setopt(g->multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
  curl_multi_setopt(g->multi, CURLMOPT_SOCKETDATA, g);
  curl_multi_setopt(g->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  curl_multi_setopt(g->multi, CURLMOPT_TIMERDATA, g);
  g->max_running = 50;
}

//int main(int argc, char **argv)
/* { */
/*   GlobalInfo g; */
/*   CURLMcode rc; */
/*   (void)argc; */
/*   (void)argv; */

/*   memset(&g, 0, sizeof(GlobalInfo)); */
/*   g.loop = ev_default_loop(0); */

/*   init_fifo(&g); */
/*   g.multi = curl_multi_init(); */

/*   ev_timer_init(&g.timer_event, timer_cb, 0., 0.); */
/*   g.timer_event.data = &g; */
/*   g.fifo_event.data = &g; */
/*   curl_multi_setopt(g.multi, CURLMOPT_SOCKETFUNCTION, sock_cb); */
/*   curl_multi_setopt(g.multi, CURLMOPT_SOCKETDATA, &g); */
/*   curl_multi_setopt(g.multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb); */
/*   curl_multi_setopt(g.multi, CURLMOPT_TIMERDATA, &g); */

/*   /1* we don't call any curl_multi_socket*() function yet as we have no handles */
/*      added! *1/ */

/*   ev_loop(g.loop, 0); */
/*   curl_multi_cleanup(g.multi); */
/*   return 0; */
/* } */
