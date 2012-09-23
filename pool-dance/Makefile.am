MAINTAINERCLEANFILES = \
	Makefile.in

sbin_PROGRAMS = pool-dance

pool_dance_CFLAGS = \
	-Wall \
	$(EVD_CFLAGS) \
	-DENABLE_TESTS="\"$(enable_tests)\""

if ENABLE_DEBUG
pool_dance_CFLAGS += -Werror -g3 -O0 -ggdb
else
pool_dance_CFLAGS += -DG_DISABLE_ASSERT -DG_DISABLE_CHECKS
endif

pool_dance_LDADD = \
	$(EVD_LIBS) \
	-lgcrypt

source_c = \
	file-logger.c \
	work-result.c \
	block-monitor.c \
	upstream-service.c \
	event-dispatcher.c \
	pool-server.c \
	work-validator.c \
	round-manager.c

source_h = \
	file-logger.h \
	work-request.h \
	work-result.h \
	block-monitor.h \
	upstream-service.h \
	event-dispatcher.h \
	pool-server.h \
	work-validator.h \
	round-manager.h

pool_dance_SOURCES = \
	main.c \
	$(source_c) $(source_h)
