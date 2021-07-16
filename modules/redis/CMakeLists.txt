find_package(Hiredis)

module_switch(ENABLE_REDIS "Enable redis module" HIREDIS_FOUND)
if (NOT ENABLE_REDIS)
  return()
endif()

set(REDIS_SOURCES
    "redis-parser.h"
    "redis.h"
    "redis-worker.h"
    "redis-worker.c"
    "redis-parser.c"
    "redis.c"
)

add_module(
  TARGET redis
  GRAMMAR redis-grammar
  INCLUDES ${HIREDIS_INCLUDE_DIR}
  DEPENDS ${HIREDIS_LIBRARIES}
  SOURCES ${REDIS_SOURCES}
)

