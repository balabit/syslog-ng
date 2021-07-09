set(REGEXPPARSER_SOURCES
    regexp-parser.c
    regexp-parser.h
    regexp-parser-parser.c
    regexp-parser-parser.h
    regexp-parser-plugin.c
)

add_module(
  TARGET regexp-parser
  GRAMMAR regexp-parser-grammar
  SOURCES ${REGEXPPARSER_SOURCES}
)

add_test_subdirectory(tests)
