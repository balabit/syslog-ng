if ENABLE_REDIS
module_LTLIBRARIES			+=	\
	modules/redis/libredis.la

modules_redis_libredis_la_CFLAGS	=	\
    $(AM_CFLAGS) \
	$(HIREDIS_CFLAGS)
modules_redis_libredis_la_CPPFLAGS	=	\
    $(AM_CPPFLAGS) \
	-I$(top_srcdir)/modules/redis		\
	-I$(top_builddir)/modules/redis
modules_redis_libredis_la_SOURCES	=	\
	modules/redis/redis-grammar.y 		\
	modules/redis/redis.c			\
	modules/redis/redis.h			\
	modules/redis/redis-worker.h	\
	modules/redis/redis-worker.c 	\
	modules/redis/redis-parser.c		\
	modules/redis/redis-parser.h
modules_redis_libredis_la_LIBADD	=	\
	$(HIREDIS_LIBS) $(MODULE_DEPS_LIBS)
modules_redis_libredis_la_LDFLAGS	=	\
	$(MODULE_LDFLAGS)
modules_redis_libredis_la_DEPENDENCIES	=	\
	$(MODULE_DEPS_LIBS)

modules/redis modules/redis/ mod-redis: \
	modules/redis/libredis.la
else
modules/redis modules/redis/ mod-redis:
endif

BUILT_SOURCES				+=	\
	modules/redis/redis-grammar.y		\
	modules/redis/redis-grammar.c		\
	modules/redis/redis-grammar.h
EXTRA_DIST				+=	\
	modules/redis/redis-grammar.ym	\
	modules/redis/CMakeLists.txt

.PHONY: modules/redis/ mod-redis
