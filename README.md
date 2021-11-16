ipcbuf - test/report the size of an IPC kernel buffer
=====================================================

Different forms of IPC utilize different in-kernel
buffers, depending on a variety of possibly
system-specific variables or settings, including
default max values, hard-code kernel limits, run-time
configurable settings such as `sysctl(8)`s etc.

To determine what the size of such a buffer might be,
the `ipcbuf` command can be used in one of two ways:
writing variable- or fixed-sized chunks of data in an
loop until the buffer is filled, or by writing
a fixed-size number of consecutive chunks.

Doing so lets you analyze the buffer sizes and factors
that influence them, as illustrated in this analysis:
https://www.netmeister.org/blog/ipcbufs.html

For full details on the usage of the command, please
see the manual page below.

Pull requests, contributions, and corrections welcome!

Installation
============

To install the command and manual page somewhere
convenient, run `make install`; the Makefile defaults
to '/usr/local' but you can change the PREFIX:

```
$ make PREFIX=~ install
```

Platforms
=========

`ipcbuf`was developed on a NetBSD 9.2 system.  It was
tested and confirmed to build and work on

- NetBSD 9.2
- FreeBSD 13.0
- macOS 11.6.1
- Linux 5.8.15 / Fedora 33
- OmniOS 5.11

---

```
NAME
     ipcbuf -- test and report on the size of an IPC kernel buffer

SYNOPSIS
     ipcbuf [-chlq] [-P size] [-R size] [-S size] [-n num] [-s type] [-t type]
	    chunk [chunk|inc]

DESCRIPTION
     The ipcbuf tool tries to determine the maximum size of the buffer used
     for the given IPC method.	To do that, it writes data to the given form
     of IPC until the write operation would block and then reports the total
     size of bytes written.

OPTIONS
     The following options are supported by ipcbuf:

     -P size  Try to set the pipe's size to size bytes.	 (Note: Linux only.)

     -R size  Try to set the SO_RCVBUF size to size bytes (socket/socketpair
	      only).

     -S size  Try to set the SO_SNDBUF size to size bytes (socket/socketpair
	      only).

     -c	      Write one or -n consecutive, fixed size chunks.

     -h	      Display help and exit.

     -l	      Write data in a loop.  This is the default mode.

     -n num   When writing chunks (see -c), write this many (additional)
	      chunks.

     -q	      Be quiet and only print the final buffer size that was deter-
	      mined.

     -s type  Use a socket/socketpair of this type.  Can be "dgram" or
	      "stream" for PF_LOCAL sockets and socketpairs or "inet-dgram",
	      "inet6-dgram", "inet-stream", "inet6-stream" for network sock-
	      ets.  Defaults to "dgram".

     -t type  Specify the type of IPC to test.	Must be one of "pipe", "fifo",
	      "socket", or "socketpair".  Defaults to "pipe".

     If an additional chunk argument is specified, then ipcbuf will begin
     writing data with a chunk of this many bytes, otherwise a single byte.

     If a second chunk|inc argument is specified, then ipcbuf will increment
     the initial chunk by this many bytes when in "loop" mode (the default, or
     when -l is specified), or, if in "chunk" mode (i.e., if -c is specified),
     write a second chunk of this many bytes.  If -n num is given, and only a
     single chunk was specified, then write num chunks of the given size; if a
     second chunk was given, then write num additional chunks of that size.

DETAILS
     Different forms of interprocess communication (IPC) utilize different
     internal kernel structures and buffers.  The size of these buffers may be
     exposed to the user via constants or e.g. a sysctl(8), but may be diffi-
     cult to determine specifically at runtime and differ across different
     operating systems.

     The ipcbuf tool will attempt to report the accurate, observed maximum
     size of the buffer in question.  It does so by writing increasing amounts
     of data into such an IPC buffer until the operation would block (or oth-
     erwise fail), then reports the total number of bytes written.

     Different forms of IPC behave differently whether data is written in
     small or increasing chunks versus large chunks, and the total number of
     data that was successfully written may end up being different from what
     the system exposes to the user.

     ipcbuf supports two different modes of writing data: "loop" and "chunk"
     mode.  In "loop" mode, ipcbuf starts writing a small a mount of data and
     then increments how many bytes it writes until the buffer is full.	 In
     "chunk" mode, ipcbuf simply writes two or more chunks of the given size.

     In addition, ipcbuf tries to report the size of the IPC buffer as best as
     it is exposed to the user.

EXAMPLES
     The following examples illustrate common usage of this tool.

     To simply report the default size of the buffer used for a pipe(2):

	   ipcbuf

     To attempt to write 4 consecutive chunks of size 16385 each:

	   ipcbuf -n 4 -c 16385

     To write a chunk of 1024 bytes into a fifo, then increment the chunk in a
     loop by 512 bytes:

	   ipcbuf -t fifo 1024 512

     To write as many 2048 byte sized chunks as will fit into a socketpair(2:)

	   ipcbuf -t socketpair 2048 0

     To write a chunk of 256 bytes into a STREAM socket, then write 257, then
     258, then 259, ... bytes until the socket buffer i full:

	   ipcbuf -t socket -s stream 256 1

     To only print out the observed buffer size of a socket after writing one
     chunk of 2560 bytes and a second chunk of 512 bytes into a UDP socket:

	   ipcbuf -q -c -t socket 2560 512

     To see the difference between a normal and a "big pipe" on NetBSD:

	   ipcbuf 16384
	   ipcbuf 16385

EXIT STATUS
     The ipcbuf utility exits 0 on success, and >0 if an error occurs.

SEE ALSO
     fcntl(2), mkfifo(2), pipe(2), socket(2), socketpair(2), sysctl(8)

HISTORY
     ipcbuf was originally written by Jan Schaumann <jschauma@netmeister.org>
     in Novermber 2021.

BUGS
     Please file bugs and feature requests by emailing the author.
```
