#	$OpenBDS: Makefile.inc,v 1.6 1996/08/21 03:47:21 tholo Exp $

# stdlib sources
.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}/stdlib ${.CURDIR}/stdlib

SRCS+=	a64l.c abort.c atexit.c atoi.c atof.c atol.c bsearch.c calloc.c \
	cfree.c exit.c getenv.c getopt.c getsubopt.c heapsort.c l64a.c \
	malloc.c merge.c multibyte.c putenv.c qsort.c radixsort.c rand.c \
	random.c realpath.c setenv.c strtod.c strtol.c strtoq.c strtoul.c \
	strtouq.c system.c tfind.c tsearch.c \
	_rand48.c drand48.c erand48.c jrand48.c lcong48.c lrand48.c \
	mrand48.c nrand48.c seed48.c srand48.c qabs.c qdiv.c

.if   (${MACHINE_ARCH} == "m68k")
SRCS+=	abs.S div.c labs.c ldiv.c
LSRCS+=	abs.c
.elif (${MACHINE_ARCH} == "i386")
SRCS+=	abs.S div.S labs.S ldiv.S
LSRCS+=	abs.c div.c labs.c ldiv.c
.elif (${MACHINE_ARCH} == "ns32k")
SRCS+=	abs.S div.c labs.c ldiv.c
LSRCS+=	abs.c
.elif (${MACHINE_ARCH} == "tahoe")
SRCS+=	abs.S div.c labs.c ldiv.c
LSRCS+=	abs.c
.elif (${MACHINE_ARCH} == "vax")
SRCS+=	abs.c div.c labs.c ldiv.c
.elif (${MACHINE_ARCH} == "alpha")
# XXX should be .S's
SRCS+=	abs.c div.c labs.c ldiv.c
.else
SRCS+=	abs.c div.c labs.c ldiv.c
.endif

MAN+=	a64l.3 abort.3 abs.3 alloca.3 atexit.3 atof.3 atoi.3 atol.3 bsearch.3 \
	calloc.3 div.3 exit.3 getenv.3 getopt.3 getsubopt.3 labs.3 ldiv.3 \
	malloc.3 memory.3 qabs.3 qdiv.3 qsort.3 radixsort.3 rand48.3 rand.3 \
	random.3 realpath.3 strtod.3 strtol.3 strtoul.3 system.3 tsearch.3

MLINKS+=getenv.3 setenv.3 getenv.3 unsetenv.3 getenv.3 putenv.3
MLINKS+=malloc.3 free.3 malloc.3 realloc.3
MLINKS+=malloc.3 cfree.3
MLINKS+=qsort.3 heapsort.3 qsort.3 mergesort.3
MLINKS+=rand.3 srand.3
MLINKS+=random.3 initstate.3 random.3 setstate.3 random.3 srandom.3
MLINKS+=rand48.3 drand48.3 rand48.3 erand48.3 rand48.3 lrand48.3
MLINKS+=rand48.3 mrand48.3 rand48.3 nrand48.3 rand48.3 jrand48.3
MLINKS+=rand48.3 srand48.3 rand48.3 seed48.3 rand48.3 lcong48.3
MLINKS+=strtol.3 strtoq.3
MLINKS+=strtoul.3 strtouq.3
MLINKS+=tsearch.3 tfind.3
MLINKS+=tsearch.3 tdelete.3
MLINKS+=tsearch.3 twalk.3
MLINKS+=a64l.3 l64a.3
