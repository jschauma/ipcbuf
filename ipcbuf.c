/* ipcbuf - test/report the size of an IPC kernel buffer
 *
 * See the following link for a discussion of the use
 * of this tool:
 * https://www.netmeister.org/blog/ipcbufs.html
 *
 * https://github.com/jschauma/ipcbuf/ --
 * pull requests, contributions, and corrections welcome!
 *
 *
 * Copyright (c) 2021, Jan Schaumann <jschauma@netmeister.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#ifdef __linux
/* Needed for F_SETPIPE_SZ */
#define _GNU_SOURCE
#include <linux/sockios.h>
#endif

#if !defined(__sun) && !defined(__linux)
#include <sys/sysctl.h>
#endif

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
	LOOP,
	CHUNK
};

enum {
	IPC_PIPE,
	IPC_FIFO,
	IPC_SOCKETPAIR,
	IPC_SOCKET
};

int IPC_TYPE = IPC_PIPE;
int MODE = LOOP;
int CHUNK1 = 1;
int CHUNK2 = -1;
int TOTAL = 0;
int NUM_CHUNKS = 1;
int LARGEST_CHUNK = 0;
int QUIET = 0;

int SET_RCVBUF = -1;
int SET_SNDBUF = -1;
int SET_PIPEBUF = -1;

char *SET_SOCKTYPE = "DGRAM";
int SOCK_TYPE = SOCK_DGRAM;
char *SET_SOCKDOMAIN = "PF_LOCAL";
int SOCK_DOMAIN = PF_LOCAL;

#define PROGNAME "ipcbuf"

int
printFdQueueSize(int fd, const char *which) {
	int req;

	/* We may not have any of these at all, so
	 * let's silence compiler warnings about
	 * unused variables. */
	(void)req; (void)fd; (void)which;

	if (strcmp(which, "space") == 0) {
#ifdef FIONSPACE
		req = FIONSPACE;
		which = "FIONSPACE";
#else
		return -1;
#endif
	}

	if (strcmp(which, "write") == 0) {
#if defined(FIONWRITE)
		req = FIONWRITE;
		which = "FIONWRITE";
#elif defined(SIOCOUTQ)
		req = SIOCOUTQ;
		which = "SIOCOUTQ";
#else
		return -1;
#endif
	} else if (strcmp(which, "read") == 0) {
#if defined(FIONREAD)
		req = FIONREAD;
		which = "FIONREAD";
#elif defined(SIOCINQ)
		req = SIOCINQ;
		which = "SIOCINQ";
#else
		return -1;
#endif
	}

/* Looks like FreeBSD doesn't support FION* for pipes or fifos,
   likewise for Linux and SIOC*. */
#if defined(__FreeBSD__) || defined(__linux)
	if ((IPC_TYPE == IPC_SOCKET) || (IPC_TYPE == IPC_SOCKETPAIR)) {
#endif
		int n;
		if (ioctl(fd, req, &n) == -1) {
			err(EXIT_FAILURE, "ioctl");
			/* NOTREACHED */
		}
		if (!QUIET) {
			(void)printf("%-15s: %8d\n", which, n);
		}
		return n;
#if defined(__FreeBSD__) || defined(__linux)
	}
#endif
	return -1;
}

void
printSockOpt(int fd, int opt) {
	if (QUIET) {
		return;
	}

	int n;
	char *sopt;
	unsigned int s = sizeof(n);
	
	switch(opt) {
	case SO_SNDBUF:
		sopt = "SO_SNDBUF";
		break;
	case SO_SNDLOWAT:
		sopt = "SO_SNDLOWAT";
		break;
	case SO_RCVBUF:
		sopt = "SO_RCVBUF";
		break;
	default:
		return;
	}

	if (getsockopt(fd, SOL_SOCKET, opt, (void *)&n, &s) < 0) {
		err(EXIT_FAILURE, "getsockopt");
		/* NOTREACHED */
	}

	(void)printf("%-15s: %8d\n", sopt, n);
}

int
writeChunk(int fd, int count) {
	char *buf;
	int n, wanted, failed = 0;

	wanted = count;
	if ((buf = malloc(count)) == NULL) {
		err(EXIT_FAILURE, "malloc");
	}
	memset(buf, 'x', count);
	again:
	if ((n = write(fd, buf, count)) < 0) {
		/* EAGAIN / EWOULDBLOCK: I/O would have been blocked;
		 * EMSGSIZE:             chunk > internal buffer size;
		 * ENOBUFS:              buffer queue is full */
		if ((errno == EMSGSIZE) || (errno == ENOBUFS)) {
			count--;
			if (count < 1) {
				(void)fprintf(stderr, "Unable to write even a single byte: %s\n", strerror(errno));
				free(buf);
				return -1;
			}
			failed = 1;
			goto again;
		}
		if (errno == EAGAIN) {
			if (failed && !QUIET) {
				(void)printf("%-15s: %8d\n", "MSGSIZE", count);
			}
			free(buf);
			(void)fprintf(stderr, "Unable to write %d more byte%s: %s\n",
					count, count > 1 ? "s" : "", strerror(errno));
			return -1;
		} else {
			err(EXIT_FAILURE, "write");
		}
	}

	TOTAL += n;
	if (!QUIET) {
		(void)printf("Wrote %8d out of %8d byte%s. %s(Total: %8d)\n",
				n, wanted,
				wanted > 1 ? "s" : "",
				wanted > 1 ? "" : " ",
				TOTAL);
	}
	free(buf);

	if (n > LARGEST_CHUNK) {
		LARGEST_CHUNK = n;
	}
	return n;
}

void
writeLoop(int fd, int count, int inc) {
	int i = 0;
	while (1) {
		int n;

		n = writeChunk(fd, count);
		if ((n != count) || (n < 0)) {
			if (n > 0) {
				i++;
			}
			break;
		}

		if (inc == -1) {
			count *= 2;
		} else {
			count += inc;
		}
		i++;
	}
	if (!QUIET) {
		(void)printf("%-15s: %8d\n", "Iterations", i);
	}
}

void
usage() {
	(void)printf("Usage: %s [-chlq] [-[PRS] bufsiz] [-n num] [-s type] [-t type] [chunk] [chunk|inc]\n",
				PROGNAME);
	(void)printf("-P size      try to set the pipe's size to this many bytes (Linux only)\n");
	(void)printf("-R size      try to set the SO_RCVBUF size to this many bytes (socket/socketpair only)\n");
	(void)printf("-S size      try to set the SO_SNDBUF size to this many bytes (socket/socketpair only)\n");
	(void)printf("-c           write two consecutive chunks\n");
	(void)printf("-h           print this help\n");
	(void)printf("-l           write in a loop\n");
	(void)printf("-n num       write this many additional chunks\n");
	(void)printf("-q           be quiet and only print the final number\n");
	(void)printf("-s type      use this type of sockt ([inet[6]-]dgram or [inet[6]-]stream)\n");
	(void)printf("-t type      use this type of IPC (fifo, pipe, socket, socketpair)\n");
	(void)printf("[chunk]      initial chunk size; 1 if not given\n");
	(void)printf("[chunk|inc]  second chunk size or loop increment\n");
	(void)printf("             if not given, use first chunk size in chunk mode, double first chunk size in loop mode\n");
}

void
setPipeSize(int fd) {
	if (SET_PIPEBUF == -1) {
		/* '-P size' not given */
		return;
	}

#ifndef F_SETPIPE_SZ
	(void)fd;
	(void)fprintf(stderr, "Sorry, setting the pipe size is not supported on this platform.\n");
	exit(EXIT_FAILURE);
	/* NOTREACHED */
#else
	if ((fcntl(fd, F_SETPIPE_SZ, SET_PIPEBUF)) < 0) {
		err(EXIT_FAILURE, "fcntl(F_SETPIPE_SZ)");
		/* NOTREACHED */
	}
#endif
}

int
inputNumber(const char *in, int threshold) {
	int n;
	if ((n = (int)strtol(in, NULL, 10)) < threshold) {
		(void)fprintf(stderr, "Please provide a number >= %d.\n", threshold);
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	}
	return n;
}

void
parseArgs(int argc, char **argv) {
	extern char *optarg;
	extern int optind;
	int ch;
	int sflag = 0;

	char *type = NULL;

	while ((ch = getopt(argc, argv, "P:R:S:chln:qs:t:")) != -1) {
		switch(ch) {
		case 'P':
			SET_PIPEBUF = inputNumber(optarg, 1);
			break;
		case 'R':
			SET_RCVBUF = inputNumber(optarg, 1);
			break;
		case 'S':
			SET_SNDBUF = inputNumber(optarg, 1);
			break;
		case 'c':
			MODE = CHUNK;
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			/* NOTREACHED */
			break;
		case 'l':
			MODE = LOOP;
			break;
		case 'n':
			NUM_CHUNKS = inputNumber(optarg, 0);
			break;
		case 'q':
			QUIET = 1;
			break;
		case 's':
			SET_SOCKTYPE = optarg;
			sflag = 1;
			break;
		case 't':
			type = optarg;
			break;
		case '?':
		default:
			usage();
			exit(EXIT_FAILURE);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 2) {
		usage();
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	}

	if (type) {
		if (strcasecmp(type, "fifo") == 0) {
			IPC_TYPE = IPC_FIFO;
		} else if (strcasecmp(type, "pipe") == 0) {
			IPC_TYPE = IPC_PIPE;
		} else if (strcasecmp(type, "socket") == 0) {
			IPC_TYPE = IPC_SOCKET;
		} else if (strcasecmp(type, "socketpair") == 0) {
			IPC_TYPE = IPC_SOCKETPAIR;
		}
	}

	if ((IPC_TYPE != IPC_PIPE) && (SET_PIPEBUF != -1)) {
		(void)fprintf(stderr, "Setting the pipe size only makes sense with '-t pipe'.\n");
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	}

	if (sflag && (IPC_TYPE != IPC_SOCKET) && (IPC_TYPE != IPC_SOCKETPAIR)) {
		(void)fprintf(stderr, "Setting the socket type only makes sense with '-t socket' or '-t socketpair'.\n");
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	}

	if ((strncmp(SET_SOCKTYPE, "inet", strlen("inet")) == 0) &&
		(IPC_TYPE != IPC_SOCKET)) {
		(void)fprintf(stderr, "'inet/inet6' type sockets can only be specified with '-t socket'.\n");
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	}

	if (strncmp(SET_SOCKTYPE, "inet-", strlen("inet-")) == 0) {
		SET_SOCKDOMAIN = "PF_INET";
		SOCK_DOMAIN = PF_INET;
		SET_SOCKTYPE += strlen("inet-");
	} else if (strncmp(SET_SOCKTYPE, "inet6-", strlen("inet6-")) == 0) {
		SET_SOCKDOMAIN = "PF_INET6";
		SOCK_DOMAIN = PF_INET6;
		SET_SOCKTYPE += strlen("inet6-");
	}

	if (strcasecmp(SET_SOCKTYPE, "stream") == 0) {
		SOCK_TYPE = SOCK_STREAM;
	} else if ((strcasecmp(SET_SOCKTYPE, "dgram") != 0)) {
		(void)fprintf(stderr, "Invalid socket type. Please use one of [inet[6]-(dgram|stream).\n");
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	}

	if (argc > 0) {
		CHUNK1 = inputNumber(argv[0], 0);
	}

	if (argc > 1) {
		CHUNK2 = inputNumber(argv[1], 0);
	}
}

void
writeData(int fd) {
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		err(EXIT_FAILURE, "fcntl set flags");
		/* NOTREACHED */
	}	

	(void)printFdQueueSize(fd, "space");
	if (!QUIET) {
		(void)printf("\n");
	}

	if (MODE == LOOP) {
		writeLoop(fd, CHUNK1, CHUNK2);
	} else {
		int i = 0;
		if (CHUNK2 < 1) {
			i = 1;
			CHUNK2 = CHUNK1;
		}
		if (!QUIET) {
			int total = CHUNK1;

			(void)printf("Trying to write %d", CHUNK1);
			if (!i) {
				total = CHUNK1 + (NUM_CHUNKS * CHUNK2);
				(void)printf(" + (%d * %d) = %d",
					NUM_CHUNKS, CHUNK2, total);
			} else if ((CHUNK1 > 1) && (NUM_CHUNKS > 1)) {
				total = CHUNK1 * NUM_CHUNKS;
				(void)printf(" * %d = %d",
					NUM_CHUNKS, total);
			}
			(void)printf(" byte%s...\n", total > 1 ? "s" : "");
		}
		writeChunk(fd, CHUNK1);
		for (; i<NUM_CHUNKS; i++) {
			writeChunk(fd, CHUNK2);
		}
	}

	(void)printFdQueueSize(fd, "write");
	if (!QUIET) {
		(void)printf("Observed total : %8d\n\n", TOTAL);
	} else {
		(void)printf("%d\n", TOTAL);
	}

	if ((IPC_TYPE == IPC_PIPE) || (IPC_TYPE == IPC_FIFO)) {
		(void)close(fd);
	}
}

void
readData(int fd) {
	int left, nr;
	int total = 0;
	char *buf;

	if (!QUIET) {
		(void)printf("Draining...\n");
	}

	int bufsiz = BUFSIZ;
	if (LARGEST_CHUNK > bufsiz) {
		bufsiz = LARGEST_CHUNK;
	}

	if ((buf = malloc(bufsiz)) == NULL) {
		err(EXIT_FAILURE, "malloc");
	}

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		err(EXIT_FAILURE, "fcntl set flags");
		/* NOTREACHED */
	}	

	while (1) {
		left = printFdQueueSize(fd, "read");
		if (left == 0) {
			break;
		}

		if ((nr = read(fd, buf, bufsiz)) < 0) {
			if (errno == EAGAIN) {
				break;
			}
			err(EXIT_FAILURE, "read");
			/* NOTREACHED */
		}
		total += nr;

		if (nr == 0) {
			/* read(2) returned EOF: all data read. */
			break;
		}
	}
	
	if (!QUIET) {
		(void)printf("%-15s: %8d\n", "Read", total);
	}
}

void
reportTest(const char *fmt, ...) {
	if (QUIET) {
		return;
	}

	va_list args;

	char *mode = "loop";
	if (MODE == CHUNK) {
		mode = "chunk";
	}

	(void)printf("Testing ");
	va_start(args, fmt);
	(void)vprintf(fmt, args);
	va_end(args);
	(void)printf(" buffer size in %s mode.\n", mode);
	if (MODE == LOOP) {
		(void)printf("Loop starting with %d byte%s",
				CHUNK1, CHUNK1 > 1 ? "s" : "");
		if (CHUNK2 == -1) {
			(void)printf(" and doubling each iteration.\n");
		} else {
			(void)printf(", increasing by %d byte%s each time.\n",
					CHUNK2, CHUNK2 != 1 ? "s" : "");
		}
	} else {
		(void)printf("First chunk: %d byte%s, ",
				CHUNK1, CHUNK1 > 1 ? "s" : "");
		(void)printf("then %d more chunk%s of size %d.\n",
				NUM_CHUNKS, NUM_CHUNKS > 1 ? "s" : "",
				CHUNK2 < 0 ? CHUNK1 : CHUNK2);
	}
	(void)printf("\n");
}

void
doPipe() {
	int fd[2];

	if (pipe(fd) < 0) {
		err(EXIT_FAILURE, "pipe");
		/* NOTREACHED */
	}

	setPipeSize(fd[1]);

	reportTest("pipe");

#ifdef PIPE_BUF
	if (!QUIET) {
		(void)printf("%-15s: %8d\n", "PIPE_BUF", PIPE_BUF);
	}
#endif
#ifdef PIPE_MAX
	if (!QUIET) {
		(void)printf("%-15s: %8d\n", "PIPE_MAX", PIPE_MAX);
	}
#endif

#ifdef F_GETPIPE_SZ
	int s;
	if ((s = fcntl(fd[1], F_GETPIPE_SZ, 0)) < 0) {
		err(EXIT_FAILURE, "fcntl(F_GETPIPE_SZ)");
		/* NOTREACHED */
	}
	if (!QUIET) {
		(void)printf("%-15s: %8d\n", "F_GETPIPE_SZ", s);
	}
#endif

	long l;
	if ((l = fpathconf(fd[1], _PC_PIPE_BUF)) < 0) {
		err(EXIT_FAILURE, "fpathconf");
		/* NOTREACHED */
	}
	if (!QUIET) {
		(void)printf("%-15s: %8ld\n", "_PC_PIPE_BUF", l);
	}

	writeData(fd[1]);
	readData(fd[0]);
}

void
reportSysctl(const char *s) {
#ifdef __sun
	return;
#else
	if (QUIET || (strcmp(s, "invalid") == 0)) {
		return;
	}

	char *sysctl, *spath, *sname;
	char sval[BUFSIZ];

	memset(sval, '\0', BUFSIZ);

	if ((sysctl = strdup(s)) == NULL) {
		err(EXIT_FAILURE, "strdup");
		/* NOTREACHED */
	}

	spath = sysctl;

	if ( ((sname = strrchr(s, '.')) == NULL) || strlen(sname) < 2) {
		sname = spath;
	} else {
		sname++;
	}

	printf("%-15s: ", sname);

#  ifdef __linux
	char c;
	int fd, n;
	char path[PATH_MAX];

	while ((c = *sysctl) != '\0') {
		if (c == '.') {
			*sysctl = '/';
		}
		sysctl++;
	}

	memset(path, '\0', PATH_MAX);
	(void)strcpy(path, "/proc/sys/");
	(void)strncat(path, spath, PATH_MAX - strlen("/proc/sys/"));

	if ((fd = open(path, O_RDONLY)) < 0) {
		err(EXIT_FAILURE, "open");
		/* NOTREACHED */
	}

	if (read(fd, sval, BUFSIZ) < 0) {
		err(EXIT_FAILURE, "read");
		/* NOTREACHED */
	}
	(void)close(fd);

	if ((n = (int)strtol(sval, NULL, 10)) < 1) {
		err(EXIT_FAILURE, "strtol");
		/* NOTREACHED */
	}

	printf("%8d\n", n);
#  else
	size_t len;

	if (sysctlbyname(s, NULL, &len, NULL, 0) < 0) {
		err(EXIT_FAILURE, "sysctl");
		/* NOTREACHED */
	}

	if (len > BUFSIZ) {
		(void)fprintf(stderr, "sysctl value too large for %s\n", spath);
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	}

	if (sysctlbyname(s, sval, &len, NULL, 0) < 0) {
		err(EXIT_FAILURE, "sysctl");
		/* NOTREACHED */
	}
	printf("%8d\n", *(int *)sval);
#  endif
	free(spath);
#endif
}

void
setBufferSizes(int rfd, int wfd) {
	socklen_t s = sizeof(SET_RCVBUF);

	if ((rfd > 0) && (SET_RCVBUF >= 0)) {
		if (setsockopt(rfd, SOL_SOCKET, SO_RCVBUF, (void *)&SET_RCVBUF, s) < 0) {
/* For unknown reasons, setsockopt(2) fails with EINVAL,
 * yet still sets the value ?? */
#ifndef __sun
			err(EXIT_FAILURE, "setsockopt");
			/* NOTREACHED */
#endif
		}
	}

	if ((wfd > 0) && (SET_SNDBUF >= 0)) {
		if (setsockopt(wfd, SOL_SOCKET, SO_SNDBUF, (void *)&SET_SNDBUF, s) < 0) {
			err(EXIT_FAILURE, "setsockopt");
			/* NOTREACHED */
		}
	}
}

void
doSocketpair() {
	int fd[2];

	reportTest("socketpair %s", SET_SOCKTYPE);

	if (socketpair(PF_LOCAL, SOCK_TYPE, 0, fd) < 0) {
		err(EXIT_FAILURE, "pipe");
		/* NOTREACHED */
	}

	setBufferSizes(fd[0], fd[1]);

	/* Not the same values, but of interest either way. */
#ifdef __linux
	reportSysctl("net.unix.max_dgram_qlen");
#else
	reportSysctl("net.local.dgram.recvspace");
#endif
	printSockOpt(fd[0], SO_RCVBUF);
	printSockOpt(fd[1], SO_SNDBUF);

	writeData(fd[1]);
	readData(fd[0]);
}

void
cleanup() {
	(void)unlink("socket");
	(void)unlink("fifo");
}

void
doFifo() {
	int rfd, wfd;

	reportTest("fifo");

	if (mkfifo("fifo", 0644) < 0) {
		err(EXIT_FAILURE, "fifo");
		/* NOTREACHED */
	}
	if ((rfd = open("fifo", O_RDONLY|O_NONBLOCK)) < 0) {
		err(EXIT_FAILURE, "open");
		/* NOTREACHED */
	}
	if ((wfd = open("fifo", O_WRONLY|O_NONBLOCK)) < 0) {
		err(EXIT_FAILURE, "open");
		/* NOTREACHED */
	}

	writeData(wfd);
	readData(rfd);
}

void
doSocket() {
	int rfd, wfd;
	int pid = 0;
	int port = 12345;
	char *sysctl = "invalid";

	reportTest("%s %s socket", SET_SOCKDOMAIN, SET_SOCKTYPE);

	void *s;
	socklen_t s_size;
	struct sockaddr_storage netsock;
	struct sockaddr_un localsock;

	if ((wfd = socket(SOCK_DOMAIN, SOCK_TYPE, 0)) < 0) {
		err(EXIT_FAILURE, "socket");
		/* NOTREACHED */
	}
	rfd = wfd;

	if (SOCK_DOMAIN == PF_LOCAL) {
		localsock.sun_family = PF_LOCAL;
		(void)strncpy(localsock.sun_path, "socket", sizeof(localsock.sun_path));
		s = &localsock;
		s_size = sizeof(localsock);

		if (SOCK_TYPE == SOCK_DGRAM) {
/* Not the same values, but of interest either way. */
#ifdef __linux
			sysctl = "net.unix.max_dgram_qlen";
#else
			sysctl = "net.local.dgram.recvspace";
		} else {
			sysctl = "net.local.stream.recvspace";
#endif
		}
	} else if (SOCK_DOMAIN == PF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&netsock;

		if (inet_pton(PF_INET, "127.0.0.1", &(sin->sin_addr)) != 1) {
			err(EXIT_FAILURE, "inet_pton");
			/* NOTREACHED */
		}

		sin->sin_family = PF_INET;
		sin->sin_port = htons(port);
		s = sin;
		s_size = sizeof(*sin);

#ifndef __linux
		if (SOCK_TYPE == SOCK_DGRAM) {
			sysctl = "net.inet.udp.recvspace";
		} else {
			sysctl = "net.inet.tcp.recvspace";
		}
#endif
	} else if (SOCK_DOMAIN == PF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)&netsock;

/* For some reason FreeBSD correctly converts "::1", but then
 * bind(2) fails with EADDRNOTAVAIL. No idea why. */
#ifdef __FreeBSD__
		sin->sin6_addr = in6addr_any;
#else
		if (inet_pton(PF_INET6, "::1", &(sin->sin6_addr)) != 1) {
			err(EXIT_FAILURE, "inet_pton");
			/* NOTREACHED */
		}
#endif
		sin->sin6_family = PF_INET6;
		sin->sin6_port = htons(port);
		s = sin;
		s_size = sizeof(*sin);

#ifdef __NetBSD__
		if (SOCK_TYPE == SOCK_DGRAM) {
			sysctl = "net.inet6.udp6.recvspace";
		} else {
			sysctl = "net.inet6.tcp6.recvspace";
		}
#endif
	}

	if (bind(wfd, (struct sockaddr *)s, s_size)) {
		err(EXIT_FAILURE, "bind");
		/* NOTREACHED */
	}

	reportSysctl(sysctl);

	if (SOCK_TYPE == SOCK_STREAM) {
		if (fflush(stdout) == EOF) {
			err(EXIT_FAILURE, "fflush");
			/* NOTREACHED */
		}
		if ((pid = fork()) < 0) {
			err(EXIT_FAILURE, "fork");
			/* NOTREACHED */
		}
		if (pid) {
			if (listen(wfd, 1) < 0) {
				err(EXIT_FAILURE, "listen");
				/* NOTREACHED */
			}
			if ((rfd = accept(wfd, NULL, NULL)) < 0) {
				err(EXIT_FAILURE, "accept");
				/* NOTREACHED */
			}
			setBufferSizes(rfd, -1);
			if (waitpid(pid, NULL, 0) < 0) {
				err(EXIT_FAILURE, "waitpid");
				/* NOTREACHED */
			}
			readData(rfd);
			exit(EXIT_SUCCESS);
			/* NOTREACHED */
		} else {
			/* Parent needs a chance to listen.
			 * This is a poor approach, but hey... */
			usleep(500);
			if ((wfd = socket(SOCK_DOMAIN, SOCK_TYPE, 0)) < 0) {
				err(EXIT_FAILURE, "socket");
				/* NOTREACHED */
			}
		}
	}

	if (connect(wfd, (struct sockaddr *)s, s_size) < 0) {
		err(EXIT_FAILURE, "connect");
		/* NOTREACHED */
	}

	setBufferSizes(rfd, wfd);
	printSockOpt(wfd, SO_SNDBUF);
	printSockOpt(rfd, SO_RCVBUF);
	writeData(wfd);

	if (SOCK_TYPE == SOCK_DGRAM) {
		readData(rfd);
		(void)unlink("socket");
	}
}

int
main(int argc, char **argv) {
	parseArgs(argc, argv);

	if (atexit(cleanup) < 0) {
		err(EXIT_FAILURE, "atexit");
		/* NOTREACHED */
	}

	switch(IPC_TYPE) {
	case IPC_FIFO:
		doFifo();
		break;
	case IPC_PIPE:
		doPipe();
		break;
	case IPC_SOCKET:
		doSocket();
		break;
	case IPC_SOCKETPAIR:
		doSocketpair();
		break;
	default:
		(void)fprintf(stderr, "Unknown IPC type: %d\n", IPC_TYPE);
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	}
}
