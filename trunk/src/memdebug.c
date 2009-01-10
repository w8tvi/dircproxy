/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * memdebug.c
 *  - wrappers to memory allocation functions
 *  - memory leak tracing
 *  - buffer overrun detection
 *
 * The idea of these is that lots of extra memory is used to store
 * information about memory allocations, and to check things don't
 * get accidentally overrun.  This is purely debug, you should
 * NEVER use this in a real program.
 * --
 * @(#) $Id: memdebug.c,v 1.9 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Define MIN() */
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif /* MIN */

/* This is the structure that goes in front of EVERY chunk or hunk of
   memory alloc'd. Take a good long look at how big it is, imagine your
   memory full of these. I think its best to only use this for debugging
   purposes. Okay? */
struct memstamp {
  unsigned short magic;
  unsigned long allocnum;
  size_t size;
  char *file;
  int line;
  struct memstamp *next;
};

/* For statistics */
struct memcount {
  char *file;
  unsigned long count;
  struct memcount *next;
};

/* definition of the magic number */
#define MEMMAGIC 0xDC10

/* data put before and after the memory to detect overruns */
#define MEMPREBUFF "yankydoodledandy"
#define MEMPOSTBUFF "itwasacolddayinhell"

/* function prototypes - don't include the .h as it'll all go *boom* */
void *mem_malloc(size_t, char *, int);
void *mem_realloc(void *, size_t, char *, int);
void mem_report(char *);

/* variables */
static unsigned long memalloccount = 0L;
static unsigned long memfreecount = 0L;
static unsigned long memusage = 0L;
static struct memstamp *memstamplist = NULL;
static struct memcount *memcounts = 0;

/* Insert a new memory stamp onto the list */
static void _mem_insert(struct memstamp *ms) {
  ms->next = memstamplist;
  memstamplist = ms;
}

/* Delete a memory stamp from the list */
static void _mem_delete(struct memstamp *ms) {
  struct memstamp *msptr;

  if (memstamplist != ms) {
    msptr = memstamplist;
    while (msptr->next != NULL) {
      if (msptr->next == ms) {
        msptr->next = ms->next;
        return;
      }
      msptr = msptr->next;
    }
    fprintf(stderr, "MEM: bugger! attempted delete of phantom stamp.\n");

  } else {
    memstamplist = ms->next;
  }
}

/* Check for overruns */
static void _mem_checkpad(struct memstamp *ms, char *file, int line) {
  char *ptr;

  ptr = (char *)ms + sizeof(struct memstamp);
  if (strncmp(ptr, MEMPREBUFF, strlen(MEMPREBUFF))) {
    char *data;

    data = (char *)malloc(strlen(MEMPREBUFF) + 1);
    strncpy(data, ptr, strlen(MEMPREBUFF));
    data[strlen(MEMPREBUFF)] = 0;

    fprintf(stderr, "MEM: possible underun detected by (%s/%d) "
           "alloc at (%s/%d).\n", file ? file : "debug code",
           file ? line : 0, ms->file ? ms->file : "debug code",
           ms->file ? ms->line : 0);
    fprintf(stderr, "     [%s:%s]\n", MEMPREBUFF, data);
    free(data);
  }

  ptr = (char *)ptr + strlen(MEMPREBUFF) + ms->size;
  if (strncmp(ptr, MEMPOSTBUFF, strlen(MEMPOSTBUFF))) {
    char *data;

    data = (char *)malloc(strlen(MEMPOSTBUFF) + 1);
    strncpy(data, ptr, strlen(MEMPOSTBUFF));
    data[strlen(MEMPOSTBUFF)] = 0;

    fprintf(stderr, "MEM: possible overun detected by (%s/%d) "
           "alloc at (%s/%d).\n", file ? file : "debug code", file ? line : 0,
           ms->file ? ms->file : "debug code", ms->file ? ms->line : 0);
    fprintf(stderr, "     [%s:%s]\n", MEMPOSTBUFF, data);
    free(data);
  }
}

/* Wrapper around malloc() which adds a memstamp to the front */
void *mem_malloc(size_t size, char *file, int line) {
  struct memstamp *ms;
  struct memcount *mc;

  if (size > 0) {
    unsigned long malloc_sz;
    char *preptr, *postptr;

    malloc_sz = (sizeof(struct memstamp) + strlen(MEMPREBUFF) + size
                 + strlen(MEMPOSTBUFF));

    if (!(ms = (struct memstamp *)malloc(malloc_sz))) {
      fprintf(stderr, "MEM: malloc failed to alloc %lu(%lu) bytes (%s/%d)\n",
              (unsigned long)malloc_sz, (unsigned long)size,
              file ? file : "debug code", file ? line : 0);
      abort();
    }

    if (file && strlen(file)) {
      mc = memcounts;
      while (mc) {
        if (!strcmp(mc->file, file)) {
          mc->count++;
          break;
        }

        mc = mc->next;
      }
      if (!mc) {
        if (!(mc = (struct memcount *)malloc(sizeof(struct memcount)))) {
          fprintf(stderr, "MEM: malloc failed to alloc %lu bytes (%s/%d)\n",
                  (unsigned long)(sizeof(struct memcount)), file, line);
          abort();
        }
   
        if (!(mc->file = (char *)malloc(strlen(file) + 1))) {
          fprintf(stderr, "MEM: malloc failed to alloc %lu bytes (%s/%d)\n",
                  (unsigned long)(strlen(file) + 1), file, line);
          abort();
        }
   
        strcpy(mc->file, file);
        mc->count = 1;
        mc->next = memcounts;
        memcounts = mc;
      }

      if (!(ms->file = (char *)malloc(strlen(file) + 1))) {
        fprintf(stderr, "MEM: malloc failed to alloc %lu bytes (%s/%d)\n",
                (unsigned long)(strlen(file) + 1), file, line);
        abort();
      }
      strcpy(ms->file, file);
      ms->allocnum = memalloccount++;
      ms->line = line;

#if 0
      fprintf(stderr, "MEM: malloc of %lu bytes (%s/%d)\n",
              (unsigned long)size, file, line);
#endif
    } else {
      ms->file = 0;
      ms->allocnum = 0;
      ms->line = 0;
    }
    ms->magic = MEMMAGIC;
    ms->size = size;
    memusage += size;
    _mem_insert(ms);

    preptr = (char *)ms;
    preptr += sizeof(struct memstamp);
    strncpy(preptr, MEMPREBUFF, strlen(MEMPREBUFF));
    preptr += strlen(MEMPREBUFF);

    postptr = preptr;
    postptr += size;
    strncpy(postptr, MEMPOSTBUFF, strlen(MEMPOSTBUFF));

    return (void *)preptr;
  } else {
    return NULL;
  }
}

/* Wrapper around realloc() which adds a memstamp to the front */
void *mem_realloc(void *ptr, size_t size, char *file, int line) {
  unsigned long data_off;
  struct memstamp *ms;
  void *block;

  if (ptr == NULL)
    return mem_malloc(size, file, line);

  data_off = sizeof(struct memstamp) + strlen(MEMPREBUFF);

  ms = (struct memstamp *)((char *)ptr - data_off);
  if (ms->magic != MEMMAGIC) {
    fprintf(stderr, "MEM: %s of illegal block (%s/%d)\n",
            (size > 0) ? "realloc" : "free", file ? file : "debug code",
            file ? line : 0);
    return NULL;
  }

  _mem_checkpad(ms, file, line);

  block = NULL;

  if (size > 0) {
    block = mem_malloc(size, file, line);
    if (size < ms->size)
      memcpy(block, (char *)ms + data_off, size);
    else
      memcpy(block, (char *)ms + data_off, ms->size);
  }

  if (ms->file)
    memfreecount++;
  memusage -= ms->size;
  _mem_delete(ms);
  free(ms->file);
  free(ms);
  return block;
}

/* Reports current memory usage and shows what was malloc()d where */
void mem_report(char *message) {
  struct memstamp *msptr;
  struct memcount *mc;
  
  msptr = memstamplist;
  printf("MEM:REPORT%s%s%s %lu bytes in use [%lu alloc][%lu free]\n",
         message ? " (" : "", message ? message : "", message ? ")" : "",
         memusage, memalloccount, memfreecount);

  mc = memcounts;
  while (mc) {
    unsigned long i, t;

    printf("%16s %8lu ", mc->file, mc->count);

    t = ((double)mc->count / (double)memalloccount) * 40;
    for (i = 0; i < t; i++)
      printf("#");
    printf("\n");

    mc = mc->next;
  }
  printf("\n");
  
  while (msptr) {
    if (message) {
      int i;
      
      printf("     %s/%d - %lu bytes alloc'd [an:%lu]\n",
             msptr->file ? msptr->file : "debug code",
             msptr->file ? msptr->line : 0, (unsigned long)msptr->size,
             msptr->file ? msptr->allocnum : 0);

      printf("     [");
      for (i = 0; i <= MIN(msptr->size, 70); i++) {
        int c;

        c = *((char *)msptr + sizeof(struct memstamp) + strlen(MEMPREBUFF) + i);
        if ((c >= 32) && (c <= 127)) {
          printf("%c", c);
        } else if (c) {
          printf("£");
        } else {
          break;
        }
      }
      printf("]\n");
    }
    _mem_checkpad(msptr, "report", 0);
    msptr = msptr->next;
  }
}
