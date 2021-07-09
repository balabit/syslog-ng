include modules/add-contextual-data/Makefile.am
include modules/afamqp/Makefile.am
include modules/affile/Makefile.am
include modules/afmongodb/Makefile.am
include modules/afprog/Makefile.am
include modules/afsmtp/Makefile.am
include modules/afsocket/Makefile.am
include modules/afsql/Makefile.am
include modules/afstomp/Makefile.am
include modules/afstreams/Makefile.am
include modules/afuser/Makefile.am
include modules/appmodel/Makefile.am
include modules/azure-auth-header/Makefile.am
include modules/basicfuncs/Makefile.am
include modules/cef/Makefile.am
include modules/confgen/Makefile.am
include modules/cryptofuncs/Makefile.am
include modules/csvparser/Makefile.am
include modules/timestamp/Makefile.am
include modules/dbparser/Makefile.am
include modules/diskq/Makefile.am
include modules/examples/Makefile.am
include modules/geoip2/Makefile.am
include modules/getent/Makefile.am
include modules/graphite/Makefile.am
include modules/hook-commands/Makefile.am
include modules/http/Makefile.am
include modules/java/Makefile.am
include modules/java-modules/Makefile.am
include modules/json/Makefile.am
include modules/kafka/Makefile.am
include modules/kvformat/Makefile.am
include modules/linux-kmsg-format/Makefile.am
include modules/map-value-pairs/Makefile.am
include modules/mqtt/Makefile.am
include modules/native/Makefile.am
include modules/openbsd/Makefile.am
include modules/pacctformat/Makefile.am
include modules/pseudofile/Makefile.am
include modules/python/Makefile.am
include modules/redis/Makefile.am
include modules/riemann/Makefile.am
include modules/afsnmp/Makefile.am
include modules/secure-logging/Makefile.am
include modules/stardate/Makefile.am
include modules/syslogformat/Makefile.am
include modules/systemd-journal/Makefile.am
include modules/system-source/Makefile.am
include modules/tagsparser/Makefile.am
include modules/xml/Makefile.am
include modules/regexp-parser/Makefile.am

SYSLOG_NG_CORE_JAR=$(top_builddir)/modules/java/syslog-ng-core/libs/syslog-ng-core.jar

SYSLOG_NG_MODULES	=	\
	mod-afsocket mod-afstreams mod-affile mod-afprog \
	mod-usertty mod-amqp mod-mongodb mod-smtp mod-http mod-json \
	mod-syslogformat mod-linux-kmsg mod-pacctformat \
	mod-confgen mod-system-source mod-csvparser mod-dbparser \
	mod-basicfuncs mod-cryptofuncs mod-geoip2 mod-afstomp \
	mod-redis mod-pseudofile mod-graphite mod-riemann \
	mod-python mod-java mod-java-modules mod-kvformat mod-date \
	mod-native mod-cef mod-add-contextual-data mod-diskq mod-getent \
	mod-map-value-pairs mod-tags-parser mod-xml \
	mod-appmodel mod-openbsd mod-snmp mod-secure-logging \
	mod-mqtt mod-regexp-parser

modules modules/: ${SYSLOG_NG_MODULES}

modules_test_subdirs	=	\
	modules_afsocket modules_afstreams modules_affile \
	modules_afprog modules_usertty modules_afamqp \
	modules_afmongodb modules_afsmtp modules_http modules_json \
	modules_syslogformat modules_linux_kmsg \
	modules_pacctformat modules_confgen modules_system_source \
	modules_csvparser modules_dbparser modules_basicfuncs \
	modules_cryptofuncs modules_geoip2 modules_afstomp \
	modules_graphite modules_riemann modules_python \
	modules_secure-logging modules_systemd_journal modules_kvformat modules_date \
	modules_cef modules_diskq modules-add-contextual-data modules_getent \
	modules_map-value-pairs modules_tagsparser modules_xml modules_appmodel \
	modules_regexp-parser

EXTRA_DIST += modules/CMakeLists.txt

.PHONY: modules modules/
