/*	$NetBSD: svc.h,v 1.17 2000/06/02 22:57:56 fvdl Exp $	*/

/*
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: @(#)svc.h 1.35 88/12/17 SMI
 *	from: @(#)svc.h      1.27    94/04/25 SMI
 * $FreeBSD: src/include/rpc/svc.h,v 1.24 2003/06/15 10:32:01 mbr Exp $
 */

/*
 * svc.h, Server-side remote procedure call interface.
 *
 * Copyright (C) 1986-1993 by Sun Microsystems, Inc.
 */

#ifndef _TIRPC_SVC_H
#define _TIRPC_SVC_H

/*
 * This interface must manage two items concerning remote procedure calling:
 *
 * 1) An arbitrary number of transport connections upon which rpc requests
 * are received.  The two most notable transports are TCP and UDP;  they are
 * created and registered by routines in svc_tcp.c and svc_udp.c, respectively;
 * they in turn call xprt_register and xprt_unregister.
 *
 * 2) An arbitrary number of locally registered services.  Services are
 * described by the following four data: program number, version number,
 * "service dispatch" function, a transport handle, and a boolean that
 * indicates whether or not the exported program should be registered with a
 * local binder service;  if true the program's number and version and the
 * port number from the transport handle are registered with the binder.
 * These data are registered with the rpc svc system via svc_register.
 *
 * A service's dispatch function is called whenever an rpc request comes in
 * on a transport.  The request's program and version numbers must match
 * those of the registered service.  The dispatch function is passed two
 * parameters, struct svc_req * and SVCXPRT *, defined below.
 */

/*
 *      Service control requests
 */
#define SVCGET_VERSQUIET	1
#define SVCSET_VERSQUIET	2
#define SVCGET_CONNMAXREC	3
#define SVCSET_CONNMAXREC	4

/*
 * Operations for rpc_control().
 */
#define RPC_SVC_CONNMAXREC_SET  0	/* set max rec size, enable nonblock */
#define RPC_SVC_CONNMAXREC_GET  1
#define RPC_SVC_MTMODE_SET      52   /* set multithreading mode */
#define RPC_SVC_MTMODE_GET      53

#define RPC_SVC_THRMAX_SET      54   /* set maximum number of threads (0 = unlimited) */
#define RPC_SVC_THRMAX_GET      55   /*  - has no effect under RPC_SVC_MT_NONE
                                     *  - must be set before calling svc_run()
                                     */

#define RPC_SVC_IDLECLEANUP_SET 56   /* enable/disable cleanup of idle sockets (0 = disabled, 1 = enabled (default)) */
#define RPC_SVC_IDLECLEANUP_GET 57

/*
 * Multithreading modes
 */
#define RPC_SVC_MT_NONE         50   /* single-threaded (default) */
#define RPC_SVC_MT_AUTO         51   /* automatic multi-threading */

enum xprt_stat {
	XPRT_DIED,
	XPRT_MOREREQS,
	XPRT_IDLE
};

/*
 * Server side transport handle
 */
typedef struct __rpc_svcxprt {
	int		xp_fd;
#define	xp_sock		xp_fd
	u_short		xp_port;	 /* associated port number */
	const struct xp_ops {
	    /* receive incoming requests */
	    bool_t	(*xp_recv)(struct __rpc_svcxprt *, struct rpc_msg *);
	    /* get transport status */
	    enum xprt_stat (*xp_stat)(struct __rpc_svcxprt *);
	    /* get arguments */
	    bool_t	(*xp_getargs)(struct __rpc_svcxprt *, xdrproc_t,
				void *);
	    /* send reply */
	    bool_t	(*xp_reply)(struct __rpc_svcxprt *, struct rpc_msg *);
	    /* free mem allocated for args */
	    bool_t	(*xp_freeargs)(struct __rpc_svcxprt *, xdrproc_t,
				void *);
	    /* destroy this struct */
	    void	(*xp_destroy)(struct __rpc_svcxprt *);
	} *xp_ops;
	int		xp_addrlen;	 /* length of remote address */
	struct sockaddr_in6 xp_raddr;	 /* remote addr. (backward ABI compat) */
	/* XXX - fvdl stick this here for ABI backward compat reasons */
	const struct xp_ops2 {
		/* catch-all function */
		bool_t  (*xp_control)(struct __rpc_svcxprt *, const u_int,
				void *);
	} *xp_ops2;
	char		*xp_tp;		 /* transport provider device name */
	char		*xp_netid;	 /* network token */
	struct netbuf	xp_ltaddr;	 /* local transport address */
	struct netbuf	xp_rtaddr;	 /* remote transport address */
	struct opaque_auth xp_verf;	 /* raw response verifier */
	void		*xp_p1;		 /* private: for use by svc ops */
	void		*xp_p2;		 /* private: for use by svc ops */
	void		*xp_p3;		 /* private: for use by svc lib */
	int		xp_type;	 /* transport type */
} SVCXPRT;

/*
 * Service request
 */
struct svc_req {
	/* ORDER: compatibility with legacy RPC */
	u_int32_t	rq_prog;	/* service program number */
	u_int32_t	rq_vers;	/* service protocol version */
	u_int32_t	rq_proc;	/* the desired procedure */
	struct opaque_auth rq_cred;	/* raw creds from the wire */
	void		*rq_clntcred;	/* read only cooked cred */
	SVCXPRT		*rq_xprt;	/* associated transport */

	/* New with TI-RPC */
	caddr_t		rq_clntname;	/* read only client name */
	caddr_t		rq_svcname;	/* read only cooked service cred */
};

/*
 *  Approved way of getting address of caller
 */
#define svc_getrpccaller(x) (&(x)->xp_rtaddr)

/*
 * Operations defined on an SVCXPRT handle
 *
 * SVCXPRT		*xprt;
 * struct rpc_msg	*msg;
 * xdrproc_t		 xargs;
 * void *		 argsp;
 */
#define SVC_RECV(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))
#define svc_recv(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))

#define SVC_STAT(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)
#define svc_stat(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)

#define SVC_GETARGS(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))
#define svc_getargs(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))

#define SVC_REPLY(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))
#define svc_reply(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))

#define SVC_FREEARGS(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))
#define svc_freeargs(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))

#define SVC_DESTROY(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)
#define svc_destroy(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)

#define SVC_CONTROL(xprt, rq, in)			\
	(*(xprt)->xp_ops2->xp_control)((xprt), (rq), (in))

/*
 * Service registration
 *
 * svc_reg(xprt, prog, vers, dispatch, nconf)
 *	const SVCXPRT *xprt;
 *	const rpcprog_t prog;
 *	const rpcvers_t vers;
 *	const void (*dispatch)();
 *	const struct netconfig *nconf;
 */

#ifdef __cplusplus
extern "C" {
#endif
extern bool_t	svc_reg(SVCXPRT *, const rpcprog_t, const rpcvers_t,
			void (*)(struct svc_req *, SVCXPRT *),
			const struct netconfig *);
#ifdef __cplusplus
}
#endif

/*
 * Service un-registration
 *
 * svc_unreg(prog, vers)
 *	const rpcprog_t prog;
 *	const rpcvers_t vers;
 */

#ifdef __cplusplus
extern "C" {
#endif
extern void	svc_unreg(const rpcprog_t, const rpcvers_t);
#ifdef __cplusplus
}
#endif

/*
 * Transport registration.
 *
 * xprt_register(xprt)
 *	SVCXPRT *xprt;
 */
#ifdef __cplusplus
extern "C" {
#endif
extern void	xprt_register(SVCXPRT *);
#ifdef __cplusplus
}
#endif

/*
 * Transport un-register
 *
 * xprt_unregister(xprt)
 *	SVCXPRT *xprt;
 */
#ifdef __cplusplus
extern "C" {
#endif
extern void	xprt_unregister(SVCXPRT *);
#ifdef __cplusplus
}
#endif


/*
 * When the service routine is called, it must first check to see if it
 * knows about the procedure;  if not, it should call svcerr_noproc
 * and return.  If so, it should deserialize its arguments via
 * SVC_GETARGS (defined above).  If the deserialization does not work,
 * svcerr_decode should be called followed by a return.  Successful
 * decoding of the arguments should be followed the execution of the
 * procedure's code and a call to svc_sendreply.
 *
 * Also, if the service refuses to execute the procedure due to too-
 * weak authentication parameters, svcerr_weakauth should be called.
 * Note: do not confuse access-control failure with weak authentication!
 *
 * NB: In pure implementations of rpc, the caller always waits for a reply
 * msg.  This message is sent when svc_sendreply is called.
 * Therefore pure service implementations should always call
 * svc_sendreply even if the function logically returns void;  use
 * xdr.h - xdr_void for the xdr routine.  HOWEVER, tcp based rpc allows
 * for the abuse of pure rpc via batched calling or pipelining.  In the
 * case of a batched call, svc_sendreply should NOT be called since
 * this would send a return message, which is what batching tries to avoid.
 * It is the service/protocol writer's responsibility to know which calls are
 * batched and which are not.  Warning: responding to batch calls may
 * deadlock the caller and server processes!
 */

#ifdef __cplusplus
extern "C" {
#endif
extern bool_t	svc_sendreply(SVCXPRT *, xdrproc_t, void *);
extern void	svcerr_decode(SVCXPRT *);
extern void	svcerr_weakauth(SVCXPRT *);
extern void	svcerr_noproc(SVCXPRT *);
extern void	svcerr_progvers(SVCXPRT *, rpcvers_t, rpcvers_t);
extern void	svcerr_auth(SVCXPRT *, enum auth_stat);
extern void	svcerr_noprog(SVCXPRT *);
extern void	svcerr_systemerr(SVCXPRT *);
extern int	rpc_reg(rpcprog_t, rpcvers_t, rpcproc_t,
			char *(*)(char *), xdrproc_t, xdrproc_t,
			char *);
#ifdef __cplusplus
}
#endif

/*
 * Lowest level dispatching -OR- who owns this process anyway.
 * Somebody has to wait for incoming requests and then call the correct
 * service routine.  The routine svc_run does infinite waiting; i.e.,
 * svc_run never returns.
 * Since another (co-existant) package may wish to selectively wait for
 * incoming calls or other events outside of the rpc architecture, the
 * routine svc_getreq is provided.  It must be passed readfds, the
 * "in-place" results of a select system call (see select, section 2).
 */

/*
 * Global keeper of rpc service descriptors in use
 * dynamic; must be inspected before each call to select
 */
extern int svc_maxfd;
extern fd_set svc_fdset;
#define svc_fds svc_fdset.fds_bits[0]	/* compatibility */
extern struct pollfd *svc_pollfd;
extern int svc_max_pollfd;

/*
 * a small program implemented by the svc_rpc implementation itself;
 * also see clnt.h for protocol numbers.
 */
#ifdef __cplusplus
extern "C" {
#endif
extern void rpctest_service(void);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
extern void	svc_getreq(int);
extern void	svc_getreqset(fd_set *);
extern void	svc_getreq_common(int);
struct pollfd;
extern void	svc_getreq_poll(struct pollfd *, int);

extern void	svc_run(void);
extern void	svc_exit(void);
extern bool_t rpc_control(int,void *);
#ifdef __cplusplus
}
#endif

/*
 * Socket to use on svcxxx_create call to get default socket
 */
#define	RPC_ANYSOCK	-1
#define RPC_ANYFD	RPC_ANYSOCK

/*
 * These are the existing service side transport implementations
 */

#ifdef __cplusplus
extern "C" {
#endif
/*
 * Transport independent svc_create routine.
 */
extern int svc_create(void (*)(struct svc_req *, SVCXPRT *),
			   const rpcprog_t, const rpcvers_t, const char *);
/*
 *      void (*dispatch)();             -- dispatch routine
 *      const rpcprog_t prognum;        -- program number
 *      const rpcvers_t versnum;        -- version number
 *      const char *nettype;            -- network type
 */


/*
 * Generic server creation routine. It takes a netconfig structure
 * instead of a nettype.
 */

extern SVCXPRT *svc_tp_create(void (*)(struct svc_req *, SVCXPRT *),
				   const rpcprog_t, const rpcvers_t,
				   const struct netconfig *);
        /*
         * void (*dispatch)();            -- dispatch routine
         * const rpcprog_t prognum;       -- program number
         * const rpcvers_t versnum;       -- version number
         * const struct netconfig *nconf; -- netconfig structure
         */


/*
 * Generic TLI create routine
 */
extern SVCXPRT *svc_tli_create(const int, const struct netconfig *,
			       const struct t_bind *, const u_int,
			       const u_int);
/*
 *      const int fd;                   -- connection end point
 *      const struct netconfig *nconf;  -- netconfig structure for network
 *      const struct t_bind *bindaddr;  -- local bind address
 *      const u_int sendsz;             -- max sendsize
 *      const u_int recvsz;             -- max recvsize
 */

/*
 * Connectionless and connectionful create routines
 */

extern SVCXPRT *svc_vc_create(const int, const u_int, const u_int);
/*
 *      const int fd;                           -- open connection end point
 *      const u_int sendsize;                   -- max send size
 *      const u_int recvsize;                   -- max recv size
 */

/*
 * Added for compatibility to old rpc 4.0. Obsoleted by svc_vc_create().
 */
extern SVCXPRT *svcunix_create(int, u_int, u_int, char *);

extern SVCXPRT *svc_dg_create(const int, const u_int, const u_int);
        /*
         * const int fd;                                -- open connection
         * const u_int sendsize;                        -- max send size
         * const u_int recvsize;                        -- max recv size
         */


/*
 * the routine takes any *open* connection
 * descriptor as its first input and is used for open connections.
 */
extern SVCXPRT *svc_fd_create(const int, const u_int, const u_int);
/*
 *      const int fd;                           -- open connection end point
 *      const u_int sendsize;                   -- max send size
 *      const u_int recvsize;                   -- max recv size
 */

/*
 * Added for compatibility to old rpc 4.0. Obsoleted by svc_fd_create().
 */
extern SVCXPRT *svcunixfd_create(int, u_int, u_int);

/*
 * Memory based rpc (for speed check and testing)
 */
extern SVCXPRT *svc_raw_create(void);

/*
 * svc_dg_enable_cache() enables the cache on dg transports.
 */
int svc_dg_enablecache(SVCXPRT *, const u_int);

int __rpc_get_local_uid(SVCXPRT *_transp, uid_t *_uid);


#ifdef __cplusplus
}
#endif


/* for backward compatibility */
#include <rpc/svc_soc.h>



#endif /* !_TIRPC_SVC_H */
