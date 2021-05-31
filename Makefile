PROG=gnveu
SRCS=gnveu.c
CFLAGS+=-Wall -Werror
MAN=
LDADD=-levent
DPADD=${LIBEVENT}
.include <bsd.prog.mk>
