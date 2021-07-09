module_LTLIBRARIES			+= modules/regexp-parser/libregexp-parser.la

modules_regexp_parser_libregexp_parser_la_SOURCES	=	\
	modules/regexp-parser/regexp-parser.c			\
	modules/regexp-parser/regexp-parser.h			\
	modules/regexp-parser/regexp-parser-grammar.y		\
	modules/regexp-parser/regexp-parser-parser.c		\
	modules/regexp-parser/regexp-parser-parser.h		\
	modules/regexp-parser/regexp-parser-plugin.c

modules_regexp_parser_libregexp_parser_la_CPPFLAGS	=	\
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)/modules/regexp-parser		\
	-I$(top_builddir)/modules/regexp-parser
modules_regexp_parser_libregexp_parser_la_LIBADD	=	\
	$(MODULE_DEPS_LIBS)
modules_regexp_parser_libregexp_parser_la_LDFLAGS	=	\
	$(MODULE_LDFLAGS)
modules_regexp_parser_libregexp_parser_la_DEPENDENCIES	=	\
	$(MODULE_DEPS_LIBS)

modules/regexp-parser modules/regexp-parser mod-regexp-parser: modules/regexp-parser/libregexp-parser.la
.PHONY: modules/regexp-parser/ mod-regexp-parser

BUILT_SOURCES       +=      \
  modules/regexp-parser/regexp-parser-grammar.y       \
  modules/regexp-parser/regexp-parser-grammar.c       \
  modules/regexp-parser/regexp-parser-grammar.h

EXTRA_DIST        +=      \
  modules/regexp-parser/regexp-parser-grammar.ym	\
  modules/regexp-parser/CMakeLists.txt

include modules/regexp-parser/tests/Makefile.am
