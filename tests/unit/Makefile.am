EXTRA_DIST += tests/unit/CMakeLists.txt

tests_unit_TESTS			= \
	tests/unit/test_matcher		   \
	tests/unit/test_clone_logmsg   \
	tests/unit/test_serialize 	   \
	tests/unit/test_msgparse	   \
	tests/unit/test_dnscache	   \
	tests/unit/test_findcrlf	   \
	tests/unit/test_ringbuffer	   \
	tests/unit/test_hostid		   \
	tests/unit/test_zone		   \
	tests/unit/test_pathutils	   \
	tests/unit/test_logwriter	\
	tests/unit/test_thread_wakeup	\
	tests/unit/test_format_sdata

check_PROGRAMS				+= \
	${tests_unit_TESTS}

unit_test_extra_modules			= \
	$(PREOPEN_SYSLOGFORMAT)

tests_unit_test_logwriter_LDADD		= \
	$(TEST_LDADD) $(unit_test_extra_modules)
tests_unit_test_logwriter_CFLAGS	= $(TEST_CFLAGS)

tests_unit_test_matcher_CFLAGS		= $(TEST_CFLAGS)
tests_unit_test_matcher_LDADD		= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_clone_logmsg_CFLAGS	= $(TEST_CFLAGS)
tests_unit_test_clone_logmsg_LDADD	= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_serialize_CFLAGS	= $(TEST_CFLAGS)
tests_unit_test_serialize_LDADD		= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_msgparse_CFLAGS		= $(TEST_CFLAGS)
tests_unit_test_msgparse_LDADD		= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_dnscache_CFLAGS		= $(TEST_CFLAGS)
tests_unit_test_dnscache_LDADD		= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_findcrlf_CFLAGS		= $(TEST_CFLAGS)
tests_unit_test_findcrlf_LDADD		= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_ringbuffer_CFLAGS	= $(TEST_CFLAGS)
tests_unit_test_ringbuffer_LDADD	= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_hostid_CFLAGS		= $(TEST_CFLAGS)
tests_unit_test_hostid_LDADD		= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_zone_LDADD		= \
	$(TEST_LDADD) $(unit_test_extra_modules)
tests_unit_test_zone_CFLAGS		= $(TEST_CFLAGS)

tests_unit_test_pathutils_CFLAGS	= $(TEST_CFLAGS)
tests_unit_test_pathutils_LDADD	= \
	$(TEST_LDADD) $(unit_test_extra_modules)

tests_unit_test_thread_wakeup_CFLAGS	= $(TEST_CFLAGS)
tests_unit_test_thread_wakeup_LDADD	= $(TEST_LDADD)

tests_unit_test_format_sdata_CFLAGS	= $(TEST_CFLAGS)
tests_unit_test_format_sdata_LDADD	= $(TEST_LDADD)
