/* -*- mode: c; tab-width: 4; fill-column: 78 -*- */
/* vi: set ts=4 tw=128: */

/*
thread_util.h, Copyright (c) 2014 Dave Odell <dmo2118@gmail.com>

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.  No representations are made about the suitability of this
software for any purpose.  It is provided "as is" without express or
implied warranty.
*/

#ifndef THREAD_UTIL_H
#define THREAD_UTIL_H

/* thread_util.h because C11 took threads.h. */

/* And POSIX threads because there aren't too many systems that support C11
   threads that don't already support POSIX threads.
   ...Not that it would be too hard to convert from the one to the other.
   Or to have both.
 */

/* Beware!
   Multithreading is a great way to add insidious and catastrophic bugs to
   a program. Make sure you understand the risks.

   You may wish to become familiar with race conditions, deadlocks, mutexes,
   condition variables, and, in lock-free code, memory ordering, cache
   hierarchies, etc., before working with threads.

   On the other hand, if a screenhack locks up or crashes, it's not the
   end of the world: XScreenSaver won't unlock the screen if that happens.
*/

/*
   The basic stragegy for applying threads to a CPU-hungry screenhack:

   1. Find the CPU-hungry part of the hack.

   2. Change that part so the workload can be divided into N equal-sized
      loads, where N is the number of CPU cores in the machine.
      (For example: with two cores, one core could render even scan lines,
      and the other odd scan lines.)

   2a. Keeping in mind that two threads should not write to the same memory
       at the same time. Specifically, they should not be writing to the
       same cache line at the same time -- so align memory allocation and
       memory accesses to the system cache line size as necessary.

   3. On screenhack_init, create a threadpool object. This creates N worker
      threads, and each thread creates and owns a user-defined struct.
      After creation, the threads are idle.

   4. On screenhack_frame, call threadpool_run(). Each thread simultaneously
      wakes up, calls a function that does one of the equal-sized loads,
      then goes back to sleep. The main thread then calls threadpool_wait(),
      which returns once all the worker threads have finished.

      Using this to implement SMP won't necessarily increase performance by
      a factor of N (again, N is CPU cores.). Both X11 and Cocoa on OS X can
      impose a not-insignificant amount of overhead even when simply blitting
      full-screen XImages @ 30 FPS.

      On systems with simultaneous multithreading (a.k.a. Hyper-threading),
      performance gains may be slim to non-existant.
 */

#include "aligned_malloc.h"

#if HAVE_CONFIG_H
/* For HAVE_PTHREAD. */
#	include "config.h"
#endif

#include <stddef.h>

#if HAVE_UNISTD_H
/* For _POSIX_THREADS. */
#	include <unistd.h>
#endif

#ifdef WIN32
#	include <windows.h>
#elif defined HAVE_COCOA
#	include "jwxyz.h"
#elif defined X11
#	include <X11/Xlib.h>
#elif defined ALLEGRO
#   include <allegro.h>
#endif

#include "xssemu.h"

#ifdef WIN32
unsigned hardware_concurrency();
#elif defined X11
unsigned hardware_concurrency(Display *dpy);
#elif defined ALLEGRO
unsigned hardware_concurrency(BITMAP *dpy);
#endif
/* This is supposed to return the number of available CPU cores. This number
   isn't necessarily constant: a system administrator can hotplug or
   enable/disable CPUs on certain systems, or the system can deactivate a
   malfunctioning core -- but these are rare.

   If threads are unavailable, this function will return 1.

   This function isn't fast; the result should be cached.
*/

#ifdef WIN32
unsigned thread_memory_alignment(HDC dpy);
#elif defined X11
unsigned thread_memory_alignment(Display *dpy);
#elif defined ALLEGRO
unsigned thread_memory_alignment(BITMAP *dpy);
#endif

/* Returns the proper alignment for memory allocated by a thread that is
   shared with other threads.

   A typical CPU accesses the system RAM through a cache, and this cache is
   divided up into cache lines - aligned chunks of memory typically 32 or 64
   bytes in size. Cache faults cause cache lines to be populated from
   memory. And, in a multiprocessing environment, two CPU cores can access the
   same cache line. The consequences of this depend on the CPU model:

   - x86 implements the MESI protocol [1] to maintain cache coherency between
     CPU cores, with a serious performance penalty on both Intel [1] and AMD
     [2].  Intel uses the term "false sharing" to describe two CPU cores
     accessing different memory in the same cache line.

   - ARM allows CPU caches to become inconsistent in this case [3]. Memory
     fences are needed to prevent horrible non-deterministic bugs from
     occurring.  Other CPU architectures have similar behavior to one of the
     above, depending on whether they are "strongly-orderered" (like x86), or
     "weakly-ordered" (like ARM).

   Aligning multithreaded memory accesses according to the cache line size
   neatly sidesteps both issues.

   One complication is that CPU caches are divided up into separate levels,
   and occasionally different levels can have different cache line sizes, so
   to be safe this function returns the largest cache line size among all
   levels.

   If multithreading is not in effect, this returns sizeof(void *), because
   posix_memalign(3) will error out if the alignment is set to be smaller than
   that.

   [1] Intel(R) 64 and IA-32 Architectures Optimization Reference Manual
      (Order Number: 248966-026): 2.1.5 Cache Hierarchy
   [2] Software Optimization Guide for AMD Family 10h Processors (Publication
       #40546): 11.3.4 Data Sharing between Caches
   [3] http://wanderingcoder.net/2011/04/01/arm-memory-ordering/
*/

/*
   Note: aligned_malloc uses posix_memalign(3) when available, or malloc(3)
   otherwise. As of SUSv2 (1997), and *probably* earlier, these are guaranteed
   to be thread-safe. C89 does not discuss threads, or thread safety;
   non-POSIX systems, watch out!
   http://pubs.opengroup.org/onlinepubs/7908799/xsh/threads.html
   http://pubs.opengroup.org/onlinepubs/009695399/functions/xsh_chap02_09.html
*/

/* int thread_malloc(void **ptr, Display *dpy, unsigned size); */
#ifdef WIN32
#define thread_malloc(ptr, dpy, size) \
  (aligned_malloc((ptr), thread_memory_alignment(NULL), (size)))
#else
#define thread_malloc(ptr, dpy, size) \
  (aligned_malloc((ptr), thread_memory_alignment(dpy), (size)))
#endif

/*
   This simply does a malloc aligned to thread_memory_alignment(). See
   above. On failure, an errno is returned, usually ENOMEM.

   It's possible for two malloc()'d blocks to at least partially share the
   same cache line. When a different thread is writing to each block, then bad
   things can happen (see thread_memory_alignment). Better malloc()
   implementations will divide memory into pools belonging to one thread or
   another, causing memory blocks belonging to different threads to typically
   be located on different memory pages (see getpagesize(2)), mitigating the
   problem in question...but there's nothing stopping threads from passing
   memory to each other. And it's not practical for the system to align each
   block to 64 or 128 byte boundaries -- it's not uncommon to need lots and
   lots of 8-32 byte allocations, and the waste could become a bit excessive.

   Some rules of thumb to take away from this:

   1. Use thread_alloc for memory that might be written to by a thread that
   didn't originally allocate the object.

   2. Use thread_alloc for memory that will be handed from one thread to
   another.

   3. Use malloc if a single thread allocates, reads from, writes to, and
   frees the block of memory.

   Oddly, I (Dave) have not seen this problem described anywhere else.
*/

#define thread_free(ptr) aligned_free(ptr)

#if HAVE_PTHREAD
#	if defined _POSIX_THREADS && _POSIX_THREADS >= 0
/*
   See The Open Group Base Specifications Issue 7, <unistd.h>, Constants for
   Options and Option Groups
   http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/unistd.h.html#tag_13_77_03_02
*/

#		include <pthread.h>

/* Most PThread synchronization functions only fail when they are misused. */
#		if defined NDEBUG
#			define PTHREAD_VERIFY(expr) (void)(expr)
#		else
#			include <assert.h>
#			define PTHREAD_VERIFY(expr) assert(!(expr))
#		endif

extern const pthread_mutex_t mutex_initializer;
extern const pthread_cond_t cond_initializer;

#	else
		/* Whatever caused HAVE_PTHREAD to be defined (configure script,
           usually) made a mistake if this is reached. */
		/* Maybe this should be a warning. */
#		error HAVE_PTHREAD is defined, but _POSIX_THREADS is not.
		/* #undef HAVE_PTHREAD */
#	endif
#endif

struct threadpool
{
/*	This is always the same as the count parameter fed to threadpool_create().
	Here's a neat trick: if the threadpool is zeroed out with a memset, and
	threadpool_create() is never called to create 0 threads, then
	threadpool::count can be used to determine if the threadpool object was
	ever initialized. */
	unsigned count;

	/* Copied from threadpool_class. No need for thread_create here, though. */
	size_t thread_size;
	void (*thread_run)(void *self);
	void (*thread_destroy)(void *self);

	void *serial_threads;

#if HAVE_PTHREAD
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	/* Number of threads waiting for the startup signal. */
	unsigned parallel_pending;

	/* Number of threads still running. During startup, this is the index of the thread currently being initialized. */
	unsigned parallel_unfinished;

	pthread_t *parallel_threads;
#endif
};

/*
   The threadpool_* functions manage a group of threads (naturally).  Each
   thread owns an object described by a threadpool_class. When
   threadpool_run() is called, the specified func parameter is called on each
   thread in parallel. Sometime after calling threadpool_run(), call
   threadpool_wait(), which waits for each thread to return from
   threadpool_class::run().

   Note that thread 0 runs on the thread from which threadpool_run is called
   from, so if each thread has an equal workload, then when threadpool_run
   returns, the other threads will be finished or almost finished. Adding code
   between threadpool_run and threadpool_wait increases the odds that
   threadpool_wait won't actually have to wait at all -- which is nice.

   If the system does not provide threads, then these functions will fake it:
   everything will appear to work normally from the perspective of the caller,
   but when threadpool_run() is called, the "threads" are run synchronously;
   threadpool_wait() does nothing.
*/

struct threadpool_class
{
	/* Size of the thread private object. */
	size_t size;

/*	Create the thread private object. Called in sequence for each thread
	(effectively) from threadpool_create.  self: A pointer to size bytes of
	memory, allocated to hold the thread object.  pool: The threadpool object
	that owns all the threads. If the threadpool is nested in another struct,
	try GET_PARENT_OBJ.  id: The ID for the thread; numbering starts at zero
	and goes up by one for each thread.  Return 0 on success. On failure,
	return a value from errno.h; this will be returned from
	threadpool_create. */
	int (*create)(void *self, struct threadpool *pool, unsigned id);

/*	Destroys the thread private object. Called in sequence (though not always
	the same sequence as create).  Warning: During shutdown, it is possible
	for destroy() to be called while other threads are still in
	threadpool_run(). */
	void (*destroy)(void *self);
};

/* Returns 0 on success, on failure can return ENOMEM, or any error code from
   threadpool_class.create. */
#ifdef WIN32
int threadpool_create(struct threadpool *self, const struct threadpool_class *cls, unsigned count);
#elif defined X11
int threadpool_create(struct threadpool *self, const struct threadpool_class *cls, Display *dpy, unsigned count);
#elif defined ALLEGRO
int threadpool_create(struct threadpool *self, const struct threadpool_class *cls, BITMAP *dpy, unsigned count);
#endif
void threadpool_destroy(struct threadpool *self);

void threadpool_run(struct threadpool *self, void (*func)(void *));
void threadpool_wait(struct threadpool *self);

#if HAVE_PTHREAD
#	define THREAD_DEFAULTS \
	"*useThreads: True",
#	define THREAD_OPTIONS \
	{"-threads",    ".useThreads", XrmoptionNoArg, "True"}, \
	{"-no-threads", ".useThreads", XrmoptionNoArg, "False"},
#else
#	define THREAD_DEFAULTS
#	define THREAD_OPTIONS
#endif

/*
   If a variable 'member' is known to be a member (named 'member_name') of a
   struct (named 'struct_name'), then this can find a pointer to the struct
   that contains it.
*/
#define GET_PARENT_OBJ(struct_name, member_name, member) (struct_name *)((char *)member - offsetof(struct_name, member_name));

#endif
