modules_regexp_parser_tests_TESTS			=	\
	modules/regexp-parser/tests/test_regexp_parser

check_PROGRAMS					+=	\
	${modules_regexp_parser_tests_TESTS}

EXTRA_DIST += modules/regexp-parser/tests/CMakeLists.txt

modules_regexp_parser_tests_test_regexp_parser_CFLAGS	=	\
	$(TEST_CFLAGS) -I$(top_srcdir)/modules/regexp-parser
modules_regexp_parser_tests_test_regexp_parser_LDADD	=	\
	$(TEST_LDADD)					\
	$(PREOPEN_SYSLOGFORMAT)				\
	-dlpreopen $(top_builddir)/modules/regexp-parser/libregexp-parser.la
