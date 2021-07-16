if ENABLE_KAFKA

modules_kafka_tests_TESTS			= \
	modules/kafka/tests/test_kafka_props \
	modules/kafka/tests/test_kafka_config \
	modules/kafka/tests/test_kafka_topic

check_PROGRAMS					+= ${modules_kafka_tests_TESTS}

modules_kafka_tests_test_kafka_props_SOURCES = 	\
	modules/kafka/tests/test_kafka-props.c

modules_kafka_tests_test_kafka_config_SOURCES = \
	modules/kafka/tests/test_kafka_config.c

modules_kafka_tests_test_kafka_topic_SOURCES = \
	modules/kafka/tests/test_kafka_topic.c

modules_kafka_tests_test_kafka_props_DEPENDENCIES =      \
        $(top_builddir)/modules/kafka/libkafka.la

modules_kafka_tests_test_kafka_config_DEPENDENCIES =	\
        $(top_builddir)/modules/kafka/libkafka.la

modules_kafka_tests_test_kafka_topic_DEPENDENCIES =      \
        $(top_builddir)/modules/kafka/libkafka.la  

modules_kafka_tests_test_kafka_props_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/kafka

modules_kafka_tests_test_kafka_config_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/kafka

modules_kafka_tests_test_kafka_topic_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/kafka

modules_kafka_tests_test_kafka_props_LDADD	= $(TEST_LDADD) 

modules_kafka_tests_test_kafka_config_LDADD	= $(TEST_LDADD) $(LIBRDKAFKA_LIBS)

modules_kafka_tests_test_kafka_topic_LDADD	= $(TEST_LDADD) $(LIBRDKAFKA_LIBS)

modules_kafka_tests_test_kafka_props_LDFLAGS	= \
	-dlpreopen $(top_builddir)/modules/kafka/libkafka.la

modules_kafka_tests_test_kafka_config_LDFLAGS	= \
	-dlpreopen $(top_builddir)/modules/kafka/libkafka.la

modules_kafka_tests_test_kafka_topic_LDFLAGS	= \
	-dlpreopen $(top_builddir)/modules/kafka/libkafka.la


endif

EXTRA_DIST += \
	modules/kafka/tests/CMakeLists.txt
