/* hipercurl */
/* Copyright © 2017 xiehuc */

/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the "Software"), */
/* to deal in the Software without restriction, including without limitation */
/* the rights to use, copy, modify, merge, publish, distribute, sublicense, */
/* and/or sell copies of the Software, and to permit persons to whom the */
/* Software is furnished to do so, subject to the following conditions: */

/* The above copyright notice and this permission notice shall be included */
/* in all copies or substantial portions of the Software. */

/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES */
/* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, */
/* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE */
/* OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "hiperfifo.h"

ssize_t min(ssize_t a, ssize_t b) {
  return a < b ? a : b;
}

static void fifo_cb(EV_P_ struct ev_io *w, int revents)
{
  GlobalInfo* g = w->data;
  ssize_t len = 0;

  while ((len = getline(&g->linebuffer, &g->capacity, stdin)) > 0) {
    char* linebuffer = g->linebuffer;
    if (strncmp(linebuffer, "http", min(4, len)) != 0) {
      direct_output(linebuffer, len, g);
      continue;
    }
    char* sep = strchr(linebuffer, '\t');
    if (sep) {
      *sep = '\0';
      new_conn(linebuffer, sep+1, g);
    } else {
      new_conn(linebuffer, NULL, g);
    }
    if (g->still_running > g->max_running) {
      ev_io_stop(g->loop, w);
      break;
    }
  }
  if (feof(stdin)) {
    ev_io_stop(g->loop, w);
  }
}

int init_fd(GlobalInfo *g)
{
  if (ev_is_active(&g->fifo_event)) {
    return 0;
  }
  ev_io_init(&g->fifo_event, fifo_cb, 0, EV_READ);
  ev_io_start(g->loop, &g->fifo_event);
  g->fifo_event.data = g;
  return 0;
}

static void usage(const char* prog)
{
  printf("usage: %s [-h] [-n<num>]\n", prog);
  printf("\t-h: print this help\n");
  printf("\t-n: set prarllel running num\n");
  exit(0);
}

static void init_args(int argc, char* argv[], GlobalInfo* g) 
{
  int ch;
  while ((ch = getopt(argc, argv, "hn:")) != -1) {
    switch (ch) {
      case 'n':
        g->max_running = atoi(optarg);
        break;
      case 'h':
      case '?':
      default:
        usage(argv[0]);
        break;
    }
  }
}

int main(int argc, char *argv[])
{
  GlobalInfo g;

  init_global(&g);
  g.start_io = init_fd;
  init_fd(&g);
  init_args(argc, argv, &g);

  ev_loop(g.loop, 0);
  
  return 0;
}
