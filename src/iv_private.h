/*
 * ivykis, an event handling library
 * Copyright (C) 2002, 2003, 2009 Lennert Buytenhek
 * Dedicated to Marija Kulikova.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version
 * 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License version 2.1 for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License version 2.1 along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "iv.h"
#include "iv_avl.h"
#include "iv_list.h"
#include "config.h"

/*
 * Per-thread state.
 */
#define NEED_SELECT

struct iv_state {
	/* iv_main.c  */
	int			quit;
	int			numobjs;

#ifndef _WIN32
	/* iv_fd.c  */
	int			numfds;
	struct iv_fd_		*handled_fd;
#endif

#ifdef _WIN32
	/* iv_handle.c  */
	HANDLE			wait;
	CRITICAL_SECTION	active_handle_list_lock;
	struct iv_list_head	active_handle_list;
	int			numhandles;
	HANDLE			handled_handle;
#endif

	/* iv_task.c  */
	struct iv_list_head	tasks;

	/* iv_timer.c  */
	struct timespec		time;
	int			time_valid;
	int			num_timers;
	struct ratnode		*timer_root;

#ifndef _WIN32
	/* poll methods  */
	union {
#ifdef HAVE_SYS_DEVPOLL_H
#undef NEED_SELECT
		struct {
			struct iv_avl_tree	fds;
			int			poll_fd;
			struct iv_list_head	notify;
		} dev_poll;
#endif

#ifdef HAVE_EPOLL_CREATE
#undef NEED_SELECT
		struct {
			int			epoll_fd;
			struct iv_list_head	notify;
		} epoll;
#endif

#ifdef HAVE_KQUEUE
#undef NEED_SELECT
		struct {
			int			kqueue_fd;
			struct iv_list_head	notify;
		} kqueue;
#endif

#ifdef HAVE_POLL
#undef NEED_SELECT
		struct {
			struct pollfd		*pfds;
			struct iv_fd_		**fds;
			int			num_regd_fds;
		} poll;
#endif

#ifdef HAVE_PORT_CREATE
#undef NEED_SELECT
		struct {
			int			port_fd;
			struct iv_list_head	notify;
		} port;
#endif

#ifdef NEED_SELECT
		struct {
			struct iv_avl_tree	fds;
			void			*sets;
			int			setsize;
			int			fd_max;
		} select;
#endif
	} u;
#endif
};

#ifdef HAVE_THREAD
extern __thread struct iv_state *__st;

static inline struct iv_state *iv_get_state(void)
{
	return __st;
}
#else
#include <pthread.h>

extern pthread_key_t iv_state_key;

static inline struct iv_state *iv_get_state(void)
{
	return pthread_getspecific(iv_state_key);
}
#endif

static inline void barrier(void)
{
	__asm__ __volatile__("" : : : "memory");
}


/*
 * Private versions of the task/timer structures, exposing their
 * internal state.  The user data fields of these structures MUST
 * match the definitions in the public header file iv.h.
 */
struct iv_task_ {
	/*
	 * User data.
	 */
	void			*cookie;
	void			(*handler)(void *);

	/*
	 * Private data.
	 */
	struct iv_list_head	list;
};

struct iv_timer_ {
	/*
	 * User data.
	 */
	struct timespec		expires;
	void			*cookie;
	void			(*handler)(void *);

	/*
	 * Private data.
	 */
	int			index;
};


/*
 * Misc internal stuff.
 */
static inline void
__iv_list_steal_elements(struct iv_list_head *oldh, struct iv_list_head *newh)
{
	struct iv_list_head *first = oldh->next;
	struct iv_list_head *last = oldh->prev;

	last->next = newh;
	first->prev = newh;

	newh->next = oldh->next;
	newh->prev = oldh->prev;

	oldh->next = oldh;
	oldh->prev = oldh;
}


/* iv_{fd,handle}.c */
void iv_poll_init(struct iv_state *st);
void iv_poll_deinit(struct iv_state *st);
void iv_poll_and_run(struct iv_state *st, struct timespec *to);

/* iv_task.c */
void iv_task_init(struct iv_state *st);
int iv_pending_tasks(struct iv_state *st);
void iv_run_tasks(struct iv_state *st);

/* iv_time_{posix,win32}.c */
void iv_time_init(struct iv_state *st);
void iv_get_time(struct timespec *time);

/* iv_timer.c */
void __iv_invalidate_now(struct iv_state *st);
void iv_timer_init(struct iv_state *st);
int iv_get_soonest_timeout(struct iv_state *st, struct timespec *to);
void iv_run_timers(struct iv_state *st);
void iv_timer_deinit(struct iv_state *st);

/* iv_tls.c */
int iv_tls_total_state_size(void);
void iv_tls_thread_init(struct iv_state *st);
void iv_tls_thread_deinit(struct iv_state *st);
