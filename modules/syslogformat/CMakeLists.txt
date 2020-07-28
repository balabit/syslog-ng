set(SYSLOGFORMAT_SOURCES
    syslog-format.c
    syslog-format.h
    syslog-format-plugin.c
    syslog-parser-parser.c
    syslog-parser-parser.h
    syslog-parser.c
    syslog-parser.h
)

add_module(
  TARGET syslogformat
  GRAMMAR syslog-parser-grammar
  SOURCES ${SYSLOGFORMAT_SOURCES}
)

add_test_subdirectory(tests)

