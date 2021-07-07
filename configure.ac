#############################################################################
# Copyright (c) 2016 Balabit
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################

dnl Process this file with autoconf to produce a configure script.
dnl
dnl There are a couple of environment defined variables which this script
dnl makes use of in addition to the standard CFLAGS/LDFLAGS/etc. These are:
dnl
dnl RELEASE_TAG        - Debian release tag which is put to debian/changelog
dnl SNAPSHOT_VERSION   - snapshot version to add to version number
dnl BINARY_BRANCH      - the value is added to all source/binary packages
dnl SOURCE_REVISION    - Revision of the source-tree, will added to the version string
dnl

AC_INIT([syslog-ng], m4_esyscmd_s([scripts/version.sh]))
AC_CONFIG_SRCDIR([syslog-ng/main.c])
AC_CONFIG_MACRO_DIR([m4])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl ***************************************************************************
dnl dependencies

GLIB_MIN_VERSION="2.32"
OPENSSL_MIN_VERSION="0.9.8"
LIBDBI_MIN_VERSION="0.9.0"
LIBRABBITMQ_MIN_VERSION="0.5.3"
LIBRDKAFKA_MIN_VERSION="1.1.0"
IVYKIS_MIN_VERSION="0.36.1"
IVYKIS_UPDATED_VERSION="0.39"
JSON_C_MIN_VERSION="0.9"
PCRE_MIN_VERSION="6.1"
LMC_MIN_VERSION="1.0.0"
LRMQ_MIN_VERSION="0.0.1"
LRC_MIN_VERSION="1.6.0"
JOURNALD_MIN_VERSION="195"
LIBSYSTEMD_MIN_VERSION="209"
LIBSYSTEMD_WITH_JOURNAL_NAMESPACES_MIN_VERSION="245"
JAVA_MIN_VERSION="1.7"
GRADLE_MIN_VERSION="3.4"
HIREDIS_MIN_VERSION="0.11.0"
CRITERION_MIN_VERSION="2.2.1"

dnl ***************************************************************************
dnl Initial setup

ostype=`uname -s`

if test -r $srcdir/dist.conf; then
        # read defaults, dist.conf does not change
        # values for parameters that are already set
        . $srcdir/dist.conf
fi

if test -z "$RELEASE_TAG"; then
        RELEASE_TAG=unstable
fi


if test "`uname -s`" = "Linux";then
        CURRDATE=`date -R`
else
        CURRDATE=`date +"%a, %d %b %Y %H:%M:%S %Z"`
fi

AM_INIT_AUTOMAKE([foreign no-define subdir-objects])
_AM_PROG_TAR([ustar])
if test -n "$SNAPSHOT_VERSION"; then
	PACKAGE_VERSION=$PACKAGE_VERSION+$SNAPSHOT_VERSION
fi

dnl ***************************************************************************
dnl script snippet to generate user friendly version strings according to the
dnl new versioning policy. All needed code are here to allow easy moving
dnl between projects.
if test -z "$RELEASE_TYPE"; then
    RELEASE_TYPE=stable
fi

if test -z "$BROCHURE_VERSION"; then
    _MAJOR=`echo $VERSION | cut -f1 -d'.'`
    _MINOR=`echo $VERSION | cut -f2 -d'.'`
    case "$RELEASE_TYPE" in
        stable) BROCHURE_VERSION="$_MAJOR";;
        feature) BROCHURE_VERSION="$_MAJOR F$_MINOR";;
        *) BROCHURE_VERSION="$VERSION dogfood edition. Please contact your syslog-ng release manager to fix the configure scritpt";;
    esac
fi

if test -z "$COMBINED_VERSION"; then
    COMBINED_VERSION="$BROCHURE_VERSION ($VERSION)"
fi

TECHNICAL_VERSION=$VERSION

AC_DEFINE_UNQUOTED(RELEASE_TYPE, "$RELEASE_TYPE", [type of syslog-ng release. stable or feature])
AC_DEFINE_UNQUOTED(BROCHURE_VERSION, "$BROCHURE_VERSION", [pretty version for users depends on the release type])
AC_DEFINE_UNQUOTED(COMBINED_VERSION, "$COMBINED_VERSION", [normal and brochure version combined])
AC_DEFINE_UNQUOTED(SOURCE_REVISION, "$SOURCE_REVISION", [source revision])
AC_SUBST(RELEASE_TYPE)
AC_SUBST(BROCHURE_VERSION)
AC_SUBST(COMBINED_VERSION)
AC_SUBST(TECHNICAL_VERSION)
dnl ***************************************************************************


if test "x$prefix" = "xNONE"; then
        prefix=$ac_default_prefix
fi
if test "x$exec_prefix" = "xNONE"; then
        exec_prefix='${prefix}'
fi
pidfiledir='${localstatedir}'
moduledir='${exec_prefix}/lib/syslog-ng'
loggenplugindir='${moduledir}/loggen'
toolsdir='${datadir}/syslog-ng/tools'
xsddir='${datadir}/syslog-ng/xsd'

config_includedir='${datadir}/syslog-ng/include'
scldir="${config_includedir}/scl"
abs_topsrcdir=`(cd $srcdir && pwd)`

AC_CONFIG_HEADERS(config.h)

dnl ***************************************************************************
dnl Arguments

AC_ARG_WITH(libnet,
   [  --with-libnet=path      use path to libnet-config script],
   ,
   [with_libnet=""])

AC_ARG_WITH(net-snmp,
   [  --with-net-snmp=path      use path to net-snmp-config script],
   ,
   [with_net_snmp=""])

AC_ARG_WITH(pidfile-dir,
   [  --with-pidfile-dir=path   Use path as the directory for storing pidfiles],
   pidfiledir=$with_pidfile_dir)

AC_ARG_WITH(module-dir,
   [  --with-module-dir=path   Use path as the directory to install modules into],
   moduledir=$with_module_dir)

AC_ARG_WITH(loggen-plugin-dir,
   [  --with-loggen-plugin-dir=path   Use path as the directory to install loggen plugins into],
   loggenplugindir=$with_loggen_plugin_dir)

AC_ARG_WITH(module-path,
   [  --with-module-path=path   Use path as the list of ':' separated directories looked up when searching for modules],
   module_path=$with_module_path)

AC_ARG_WITH(timezone-dir,
   [  --with-timezone-dir=path   Use path as the directory to get the timezone files],
   timezonedir=$with_timezone_dir)

AC_ARG_WITH(ld-library-path,
   [  --with-ld-library-path=path  Set LD_LIBRARY_PATH during runtime to the value given],
   env_ld_library_path=$with_ld_library_path)

AC_ARG_WITH([systemdsystemunitdir],
   AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]))

AC_ARG_WITH(package_name,
   [  --with-package-name=package name Package name is printed out when syslog-ng started with --version],
   PACKAGE_NAME=$with_package_name)

AC_ARG_ENABLE(forced_server_mode,
              [  --enable-forced-server-mode   Enable forced server mode.],, enable_forced_server_mode="yes")

AC_ARG_ENABLE(debug,
              [  --enable-debug      Enable debugging code.],, enable_debug="no")

AC_ARG_ENABLE(force_gnu99,
              [  --enable-force-gnu99      Enforce C99 with gnu extensions.],, force_gnu99="no")

AC_ARG_ENABLE(extra-warnings,
              [  --enable-extra-warnings      Enable extra compiler warnings (recommended).],, enable_extra_warnings="no")

AC_ARG_ENABLE(env-wrapper,
              [  --enable-env-wrapper    Enable wrapper program to set up various environment variables],, enable_env_wrapper=auto)

AC_ARG_ENABLE(gprof,
              [  --enable-gprof      Enable gcc profiling.],, enable_gprof="no")

AC_ARG_ENABLE(memtrace,
              [  --enable-memtrace   Enable alternative leak debugging code.])

AC_ARG_WITH(sanitizer,
              [  --with-sanitizer=[address/undefined/etc...]
                                         Enables compiler sanitizer supports (default: no)]
              ,,with_sanitizer="no")

AC_ARG_ENABLE(dynamic-linking,
              [  --enable-dynamic-linking        Link everything dynamically.],,enable_dynamic_linking="auto")

AC_ARG_ENABLE(mixed-linking,
              [  --enable-mixed-linking          Link 3rd party libraries statically, system libraries dynamically],,enable_mixed_linking="auto")

AC_ARG_ENABLE(ipv6,
              [  --enable-ipv6           Enable support for IPv6.],,enable_ipv6="auto")

AC_ARG_ENABLE(tcp-wrapper,
              [  --enable-tcp-wrapper    Enable support for TCP wrappers.],,enable_tcp_wrapper="auto")

AC_ARG_ENABLE(spoof-source,
              [  --enable-spoof-source   Enable support for spoofed source addresses.]
              ,,enable_spoof_source="auto")

AC_ARG_ENABLE(sun-streams,
              [  --enable-sun-streams    Enable support for Solaris /dev/log STREAMS device.]
              ,,enable_sun_streams="auto")

AC_ARG_ENABLE(openbsd-system-source,
              [  --enable-openbsd-system-source    Enable support for OpenBSD /dev/klog device.]
              ,,enable_openbsd_system_source="auto")

AC_ARG_ENABLE(sql,
	      [  --enable-sql            Enable support for SQL destinations. (default: auto)]
	      ,,enable_sql="auto")

AC_ARG_ENABLE(pacct,
              [  --enable-pacct          Enable support for reading Process Accounting files (EXPERIMENTAL, Linux only).]
              ,,enable_pacct="no")

AC_ARG_ENABLE(linux-caps,
              [  --enable-linux-caps     Enable support for managing Linux capabilities (default: auto)]
              ,,enable_linux_caps="auto")

AC_ARG_ENABLE(gcov,
              [  --enable-gcov           Enable coverage profiling (default: no)]
              ,,enable_gcov="no")

AC_ARG_ENABLE(mongodb,
	      [  --enable-mongodb        Enable mongodb destination (default: auto)]
              ,,enable_mongodb="auto")

AC_ARG_WITH(jsonc,
              [  --with-jsonc=[system/internal/auto/no]
                                         Link against the system supplied or the built-in jsonc library or explicitly disable it. (default:auto)]
              ,,with_jsonc="internal")

AC_ARG_ENABLE(json,
             [ --enable-json=[yes/no]            Enable JSON support (default: yes)]
              ,[if test "x$enableval" != "xno"; then with_jsonc="system"; else with_jsonc="no"; fi],)

AC_ARG_ENABLE(amqp,
          [  --enable-amqp        Enable amqp destination (default: auto)]
              ,,enable_amqp="auto")

AC_ARG_ENABLE(stomp,
          [  --enable-stomp        Enable stomp destination (default: yes)]
              ,,enable_stomp="yes")

AC_ARG_WITH(ivykis,
              [  --with-ivykis=[system/internal]
                                         Link against the system supplied or the built-in ivykis library.]
              ,,with_ivykis="internal")

AC_ARG_ENABLE(smtp,
              [  --disable-smtp          Disable SMTP support (default: auto)]
              ,,enable_smtp="auto")

AC_ARG_WITH(libesmtp,
             AC_HELP_STRING([--with-libesmtp=DIR],
                            [use libesmtp library from (prefix) directory DIR]),,)

AC_ARG_ENABLE(mqtt,
              [  --enable-mqtt          Disable MQTT support (default: auto)]
              ,,enable_mqtt="auto")

AC_ARG_WITH(libpaho-mqtt,
             AC_HELP_STRING([--with-libpaho-mqtt=DIR],
                            [use libpaho-mqtt library from (prefix) directory DIR]),,)


AC_ARG_ENABLE(http,
              [  --disable-http          Disable http support (default: auto)]
              ,,enable_http="auto")

AC_ARG_WITH(libcurl,
             AC_HELP_STRING([--with-libcurl=DIR],
                            [use libcurl library from (prefix) directory DIR]),,)

AC_ARG_ENABLE(redis,
              [  --disable-redis          Disable REDIS support (default: auto)]
              ,,enable_redis="auto")

AC_ARG_WITH(libhiredis,
             AC_HELP_STRING([--with-libhiredis=DIR],
                            [use libhiredis library from (prefix) directory DIR]),,)

AC_ARG_ENABLE(systemd,
              [  --enable-systemd        Enable systemd support (default: auto)]
              ,,enable_systemd="auto")

AC_ARG_ENABLE(geoip2,
              [  --enable-geoip2          Enable GeoIP2 support (default: auto)]
              ,,enable_geoip2="auto")

AC_ARG_ENABLE(riemann,
              [  --disable-riemann       Disable riemann destination]
              ,,enable_riemann="auto")

AC_ARG_WITH(compile-date,
	      [  --without-compile-date  Do not include the compile date in the binary]
	      ,wcmp_date="${withval}", wcmp_date="yes")

AC_ARG_WITH(systemd-journal,
              [  --with-systemd-journal=[system/optional/auto]
                                         Link against the system supplied or the wrapper library. (default: auto)]
              ,,with_systemd_journal="auto")

AC_ARG_ENABLE(python,
              [  --disable-python       Disable Python module]
              ,,enable_python="auto")

AC_ARG_ENABLE(kafka,
              [  --enable-kafka         Enable rdkafka support]
              ,,enable_kafka="auto")

AC_ARG_WITH(python,
              [  --with-python=VERSION    Build with a specific version of python]
              ,,with_python="auto")

AC_ARG_WITH(python3-home,
              [  --with-python3-home=path Use path as a hard-coded Python home directory that can not be overwritten by the PYTHONHOME environment variable],
              python3_home_dir=$with_python3_home)

AC_ARG_WITH(python2-home,
              [  --with-python2-home=path Use path as a hard-coded Python home directory that can not be overwritten by the PYTHONHOME environment variable],
              python2_home_dir=$with_python2_home)

AC_ARG_WITH(pylint,
              [  --with-pylint=ABSOLUTE_PATH    Specify pylint location])

AC_ARG_WITH(docbook,
            [  --with-docbook=FILE    Compiling manpages using docbook in the specified path, if not set, it will be searched on the system, or online version will be used from http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl]
            ,,with_docbook="auto")

AC_ARG_ENABLE(manpages, AC_HELP_STRING([ --enable-manpages], [ Enable generation of manpages (default: no)]), , [enable_manpages="no"; XSL_STYLESHEET=""])
AC_ARG_ENABLE(manpages-install, AC_HELP_STRING([ --enable-manpages-install], [ Enable installation of manpages (default: auto)]), , [enable_manpages_install="auto"])

AC_ARG_ENABLE(java, [ --enable-java  Enable java destination (default: auto)],, enable_java="auto")

AC_ARG_ENABLE(java-modules, [ --enable-java-modules  Compile all Java modules (default: auto)],, enable_java_modules="auto")

AC_ARG_ENABLE(native,
              [  --enable-native        Enable native bindings (default: auto)]
              ,,enable_native="auto")

AC_ARG_ENABLE(afsnmp, [ --enable-afsnmp Enable afsnmp module (default: auto)],, enable_afsnmp="auto")

AC_ARG_ENABLE(all-modules,
              [  --enable-all-modules    Forcibly enable all modules. (default: auto)]
              ,,enable_all_modules="auto")

if test "x$enable_all_modules" != "xauto"; then
   state="$enable_all_modules"

   MODULES="spoof_source sun_streams sql pacct mongodb json amqp stomp \
            redis systemd geoip2 riemann ipv6 smtp native python java java_modules \
            http afsnmp kafka mqtt"
   for mod in ${MODULES}; do
       modstate=$(eval echo \$enable_${mod})
       if test "x$modstate" = "xauto"; then
          eval enable_${mod}=${state}
       fi
   done
fi

if test "x$wcmp_date" != "xno"; then
  wcmp_date="1"
else
  wcmp_date="0"
fi

patheval()
{
        OLD=$1
        NEW=`eval echo $1`
        while test "x$OLD" != "x$NEW"
        do
                OLD=$NEW
                NEW=`eval echo $OLD`
        done
        echo $OLD
}

dnl ***************************************************************************
dnl Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
if test "x$ac_cv_prog_cc_c99" = "xno"; then
	AC_MSG_ERROR("C99 standard compliant C compiler required. Try GCC 3.x or later.")
fi
AC_PROG_YACC
AM_PROG_LEX
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG
LT_INIT([dlopen disable-static])
if test "x$with_python" = "xauto"; then
  AM_PATH_PYTHON(,,[:])
else
  AM_PATH_PYTHON([$with_python],,[:])
fi

if test "${PYTHON}" = ":"; then
       AC_MSG_WARN([Python interpreter is missing.])
fi

AM_CONDITIONAL([HAVE_PYTHON_INTERPRETER], [test "$PYTHON" != :])

dnl ***************************************************************************
dnl Validate yacc

yacc_ok=0
if echo "$YACC" | grep -q bison; then
	# NOTE: m4 removes [], that's why it needs to be escaped
	bison_version=`$YACC --version | head -n 1 | sed 's/@<:@^0-9.@:>@*//'`
	bison_version_major=`echo $bison_version | cut -d. -f1`
	bison_version_minor=`echo $bison_version | cut -d. -f2`
	bison_version_patch=`echo $bison_version | cut -d. -f3`
	if ([[ "$bison_version_major" -gt 3 ]]) ||
	   ([[ "$bison_version_major" -eq 3 ]] && [[ "$bison_version_minor" -gt 7 ]]) ||
	   ([[ "$bison_version_major" -eq 3 ]] && [[ "$bison_version_minor" -eq 7 ]] && [[ "$bison_version_patch" -ge 6 ]]); then
		yacc_ok=1
	else
		AC_MSG_WARN([bison is found, but your bison version $bison_version is not recent enough, at least 3.7.6 is required])
	fi
fi

if test $yacc_ok = 0; then
	if test -f $srcdir/lib/cfg-grammar.c || test -f lib/cfg-grammar.c; then
		AC_MSG_WARN([No proper bison found, you'll not be able to change lib/cfg-grammar.y])
		YACC="echo Required bison not found && false"
	else
		AC_MSG_ERROR([syslog-ng requires bison 3.7.6 or later. Your source tree seems to be from git, which doesn't have the bison generated files (like cfg-grammar.c). Please install/upgrade bison or use a distribution tarball.])
	fi
fi

dnl ***************************************************************************
dnl Validate flex

if $LEX --version | grep "flex" >/dev/null; then
	lex_ok=1
else
	lex_ok=0
fi

if test $lex_ok = 0 ; then
	if test -f $srcdir/lib/cfg-lex.c || test -f lib/cfg-lex.c; then
		AC_MSG_WARN([No flex found, you'll not be able to change lib/cfg-lex.l])
		LEX="echo Required flex version not found && false"
	else
		AC_MSG_ERROR([syslog-ng requires flex in order to generate its config lexer. Your source tree seems to be from git, which doesn't have lib/cfg-lex.c. Please install flex or use a distribution tarball.])
	fi
fi

dnl ***************************************************************************
dnl Set up NO_PIE_LDFLAG: -no-pie is compatible with $CC.

AX_COMPILER_VENDOR
AX_COMPILER_VERSION

if test $ostype = "Darwin"; then
  no_pie_ldflags="-Wl,-no_pie"
else
  if test x$ax_cv_c_compiler_vendor = "xgnu"; then
    AX_COMPARE_VERSION($ax_cv_c_compiler_version, gt, [4.5], [no_pie_ldflags="-no-pie"], [no_pie_ldflags=""])
  else
    no_pie_ldflags="-no-pie"
  fi;
fi

old_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $no_pie_ldflags"

AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) {return 0;}])],
	[no_pie_option_available=yes],
	[no_pie_option_available=no])

LDFLAGS="$old_LDFLAGS"

if test "$no_pie_option_available" = "yes"; then
  NO_PIE_LDFLAG="$no_pie_ldflags"
fi
AC_SUBST([NO_PIE_LDFLAG])

dnl ***************************************************************************
dnl Set up CFLAGS


dnl The rest of CFLAGS. We produce
dnl -g/-O2/-pg/-fprofile-arcs/-ftest-coverage combinations, the rest is
dnl supplied by Makefile.am

if test "x$ac_compiler_gnu" = "xyes"; then
	# by ignoring the existing $CFLAGS setting we can override the
	# default supplied by autoconf (which defaults to -O2), which
	# we don't want to use with our debugging builds.

	CFLAGS_ADD=""
        if test "x$enable_debug" = "xyes"; then
                CFLAGS_ADD="${CFLAGS_ADD} -g"
        else
                CFLAGS_ADD="${CFLAGS_ADD} -O2 -g"
        fi
        if test "x$enable_gprof" = "xyes"; then
                CFLAGS_ADD="${CFLAGS_ADD} -pg"
        fi
        if test "x$enable_gcov" = "xyes"; then
                CFLAGS_ADD="${CFLAGS_ADD} -fprofile-arcs -ftest-coverage"
        fi
else
        enable_extra_warnings="no"
fi
CFLAGS_ADD="${CFLAGS_ADD} -pthread"

dnl The current supported standard is C99 with gnu extensions

if test "x$force_gnu99" = "xyes"; then
	CFLAGS_ADD="${CFLAGS_ADD} -std=gnu99"
fi

dnl We are checking for the postive warning flag, as gcc handles the
dnl -Wno-XXX version oddly, so if no other warnings are present, it is not
dnl reported at all, causing it to be unusable for detection purposes.

AX_CFLAGS_GCC_OPTION(-Winitializer-overrides,
		     CFLAGS_INITIALIZER_OVERRIDES,
		     CFLAGS_ADD="${CFLAGS_ADD} -Wno-initializer-overrides")

dnl User supplied CFLAGS come last
CFLAGS="${CFLAGS_ADD} ${ac_cv_env_CFLAGS_value}"

AC_SYS_LARGEFILE

# FIXME: skip tests on unsupported platforms/architectures...
case "$ostype" in
	HP-UX)
		if $CC -v 2>&1 | grep gcc > /dev/null; then
			CFLAGS="${CFLAGS} -U_XOPEN_SOURCE -U_XOPEN_SOURCE_EXTENDED -D_HPUX_SOURCE"
			LDFLAGS="${LDFLAGS} -lcl"
			AC_DEFINE(HAVE_BROKEN_PREAD, 1, [define if your platform has a broken pread/pwrite (e.g. HP-UX)])
			AC_MSG_WARN([NOTE: on HP-UX with gcc, you might need to edit sys/socket.h manually or you'll get compilation errors])
		fi
		;;
	AIX)
		if test "$ac_cv_sys_large_files" -ne 0; then
			CFLAGS="${CFLAGS} -D_LARGE_FILES=1"
		fi

                # NOTE: The -brtl option to the linker must be set before calling the
                # configure script, as otherwise the generated libtool will behave
                # differently. We need the runtime linker during execution (hence the
                # -brtl) to load external modules. Also, please note that with -brtl the
                # linker behaves similarly to what is expected on other UNIX systems,
                # without it, it refuses to link an .so in if there's no reference from
                # the main program, even if there is a proper -llibname option.
		LDFLAGS="$LDFLAGS -Wl,-brtl"
		MODULE_LDFLAGS="-avoid-version -module"
		;;
	Darwin)
		MODULE_LDFLAGS="-avoid-version -dylib"
		LDFLAGS="$LDFLAGS -Wl,-flat_namespace"
		CFLAGS="$CFLAGS -D__APPLE_USE_RFC_3542"
    ;;
	OSF1)
		CFLAGS="${CFLAGS} -D_XOPEN_SOURCE=500 -D_XOPEN_SOURCE_EXTENDED -D_OSF_SOURCE -D_POSIX_C_SOURCE"
		;;
esac

if test "$enable_dynamic_linking" = "auto" -a "$enable_mixed_linking" = "auto"; then
	enable_dynamic_linking="yes"
	enable_mixed_linking="no"
fi

linkopts=0
if test "x$enable_dynamic_linking" = "xyes"; then
       linkopts=`expr $linkopts + 1`
fi
if test "x$enable_mixed_linking" = "xyes"; then
       linkopts=`expr $linkopts + 1`
fi

if test "$linkopts" -gt 1; then
       AC_MSG_ERROR([You cannot specify multiple linking options at the same time (--enable-dynamic-linking, --enable-mixed-linking).])
fi

if test "x$enable_dynamic_linking" = "xyes"; then
	enable_dynamic_linking="yes"
	enable_mixed_linking="no"
	linking_mode="dynamic"
elif test "x$enable_mixed_linking" = "xyes"; then
	enable_dynamic_linking="no"
	enable_mixed_linking="yes"
	linking_mode="mixed"
fi

dnl ***************************************************************************
dnl Check for --whole-archive flag
dnl ***************************************************************************

if test $ostype = "Darwin"; then
  WHOLE_ARCHIVE_OPT="-all_load"
  NO_WHOLE_ARCHIVE_OPT="-noall_load"
else
  WHOLE_ARCHIVE_OPT="--whole-archive"
  NO_WHOLE_ARCHIVE_OPT="--no-whole-archive"
fi

dnl ***************************************************************************
dnl Is the __thread keyword available?
dnl ***************************************************************************

AC_LINK_IFELSE([AC_LANG_PROGRAM(
[[#include <pthread.h>
__thread int a;
]],
[a=0;])],
[ac_cv_have_tls=yes; AC_DEFINE_UNQUOTED(HAVE_THREAD_KEYWORD, 1, "Whether Thread Local Storage is supported by the system")])

dnl ***************************************************************************
dnl How to do static linking?
dnl ***************************************************************************

AC_MSG_CHECKING(how to enable static linking for certain libraries)
ldversion=`ld -V 2>&1 | head -n 1`
if echo $ldversion | egrep "GNU|Solaris" > /dev/null; then
        LD_START_STATIC="-Wl,-Bstatic"
        LD_END_STATIC="-Wl,-Bdynamic"
        AC_MSG_RESULT(GNU or Solaris)
elif test $ostype = "HP-UX" > /dev/null; then
        LD_START_STATIC="-Wl,-a,archive"
        LD_END_STATIC="-Wl,-a,shared_archive"
        AC_MSG_RESULT(HP-UX)
elif test "$ostype" = "AIX"; then
        LD_START_STATIC="-Wl,-bstatic"
        LD_END_STATIC="-Wl,-bdynamic"
        AC_MSG_RESULT(AIX)
else
        LD_START_STATIC=""
        LD_END_STATIC=""
        AC_MSG_RESULT([no clues, linking everything dynamically, please send appropriate ld arguments to syslog-ng@lists.balabit.hu])
fi

dnl ***************************************************************************
dnl Miscellanneous headers
dnl ***************************************************************************

AC_HEADER_STDC
AC_CHECK_HEADER(dmalloc.h)
AC_CHECK_HEADER(dlfcn.h)
AC_CHECK_HEADERS(strings.h	\
	getopt.h		\
	stropts.h		\
	sys/strlog.h		\
	door.h			\
	sys/capability.h	\
	sys/prctl.h		\
	utmp.h			\
	utmpx.h)
AC_CHECK_HEADERS(tcpd.h)

AC_CHECK_TYPES([struct ucred, struct cmsgcred], [], [], [#define _GNU_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>])

dnl ***************************************************************************
dnl Header checks
dnl ***************************************************************************

dnl Checks for typedefs, structures, and compiler characteristics.
AC_STRUCT_TM

AC_CHECK_MEMBER(struct tm.tm_gmtoff,AC_DEFINE(HAVE_STRUCT_TM_TM_GMTOFF,1,[Whether you have tm_gmtoff field in struct tm]),,[
#if TM_IN_SYS_TIME
#include <sys/time.h>
#else
#include <time.h>
#endif])

AC_CHECK_MEMBER(struct msghdr.msg_control,AC_DEFINE(HAVE_CTRLBUF_IN_MSGHDR,1,[Whether you have msg_control field in msghdr in socket.h]),,[
#include <sys/socket.h>
])

AC_CACHE_CHECK(for I_CONSLOG, blb_cv_c_i_conslog,
  [AC_EGREP_CPP(I_CONSLOG,
[
#include <sys/strlog.h>
I_CONSLOG
],
  blb_cv_c_i_conslog=no, blb_cv_c_i_conslog=yes)])

old_CPPFLAGS=$CPPFLAGS
CPPFLAGS="-D_GNU_SOURCE -D_DEFAULT_SOURCE"
AC_CACHE_CHECK(for O_LARGEFILE, blb_cv_c_o_largefile,
  [AC_EGREP_CPP(O_LARGEFILE,
[
#include <fcntl.h>
O_LARGEFILE
],
  blb_cv_c_o_largefile=no, blb_cv_c_o_largefile=yes)])
CPPFLAGS=$old_CPPFLAGS

if test "x$blb_cv_c_o_largefile" = "xyes"; then
        AC_DEFINE(HAVE_O_LARGEFILE, 1, [O_LARGEFILE is present])
fi

AC_CACHE_CHECK(for struct sockaddr_storage, blb_cv_c_struct_sockaddr_storage,
  [AC_EGREP_HEADER([sockaddr_storage], sys/socket.h, blb_cv_c_struct_sockaddr_storage=yes,blb_cv_c_struct_sockaddr_storage=no)])

if test "$blb_cv_c_struct_sockaddr_storage" = "yes"; then
	AC_DEFINE(HAVE_STRUCT_SOCKADDR_STORAGE,[1],[struct sockaddr_storage is present on your system])
fi

AC_CACHE_CHECK(for struct sockaddr_in6, blb_cv_c_struct_sockaddr_in6,
  [AC_EGREP_HEADER([sockaddr_in6], netinet/in.h, blb_cv_c_struct_sockaddr_in6=yes,blb_cv_c_struct_sockaddr_in6=no)])

AC_CACHE_CHECK(for PR_SET_KEEPCAPS, blb_cv_keepcaps,
  [AC_EGREP_CPP(PR_SET_KEEPCAPS,
[
#include <sys/prctl.h>

PR_SET_KEEPCAPS
],
  blb_cv_keepcaps=no,
  blb_cv_keepcaps=yes)])

if test "x$blb_cv_keepcaps" = "xyes"; then
	AC_DEFINE(HAVE_PR_SET_KEEPCAPS, 1, [have PR_SET_KEEPCAPS])
fi

if test "$ostype" != "Darwin" ; then
	AC_DEFINE(HAVE_ENVIRON, [1], [Specifies whether the environ global variable exists])
fi

AC_CACHE_CHECK(for modern utmp,
               blb_cv_c_modern_utmp,
[AC_TRY_COMPILE([
#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#else
#include <utmp.h>
#endif
],
[
#ifdef HAVE_UTMPX_H
        struct utmpx ut;
#else
        struct utmp ut;
#endif
  return sizeof(ut.ut_type) & sizeof(ut.ut_user);
],
blb_cv_c_modern_utmp=yes,
blb_cv_c_modern_utmp=no)])

if test "x$blb_cv_c_modern_utmp" = "xyes"; then
	AC_DEFINE(HAVE_MODERN_UTMP, 1, [have modern utmp/utmpx format])
fi

dnl ***************************************************************************
dnl Checks for libraries
AC_CHECK_LIB(door, door_create, BASE_LIBS="$BASE_LIBS -ldoor")
AC_CHECK_LIB(socket, socket, BASE_LIBS="$BASE_LIBS -lsocket")
AC_CHECK_LIB(rt, nanosleep, BASE_LIBS="$BASE_LIBS -lrt")
AC_CHECK_FUNC(gethostbyname, [], AC_CHECK_LIB(nsl, gethostbyname, BASE_LIBS="$BASE_LIBS -lnsl"))
AC_CHECK_LIB(regex, regexec, REGEX_LIBS="-lregex")
AC_CHECK_LIB(resolv, res_init, RESOLV_LIBS="-lresolv")

AC_CHECK_FUNCS(strdup		\
	strtol			\
	strtoll			\
	strtoimax		\
	inet_aton		\
	inet_ntoa		\
	getaddrinfo		\
	getnameinfo		\
	getutent		\
	getutxent		\
	pread			\
	pwrite			\
	strcasestr		\
	memrchr			\
	localtime_r		\
	getprotobynumber_r	\
	gmtime_r		\
	strnlen			\
	strtok_r)
old_LIBS=$LIBS
LIBS=$BASE_LIBS
AC_CHECK_FUNCS(clock_gettime)
LIBS=$old_LIBS

dnl ***************************************************************************
dnl check inotify
dnl ***************************************************************************
AC_CHECK_FUNCS([inotify_init])

dnl ***************************************************************************
dnl check getrandom
dnl ***************************************************************************
AC_CHECK_FUNCS([getrandom])

dnl ***************************************************************************
dnl libevtlog headers/libraries (remove after relicensing libevtlog)
dnl ***************************************************************************

EVTLOG_LIBS="\$(top_builddir)/lib/eventlog/src/libevtlog.la"
EVTLOG_NO_LIBTOOL_LIBS="\$(top_builddir)/lib/eventlog/src/.libs/libevtlog.so"
EVTLOG_CFLAGS="-I\$(top_srcdir)/lib/eventlog/src -I\$(top_builddir)/lib/eventlog/src"

dnl ***************************************************************************
dnl secret storage libraries
dnl ***************************************************************************

SECRETSTORAGE_LIBS="\$(top_builddir)/lib/secret-storage/libsecret-storage.la"
SECRETSTORAGE_NO_LIBTOOL_LIBS="\$(top_builddir)/lib/secret-storage/.libs/libsecret-storage.so"

dnl ***************************************************************************
dnl libwrap headers/libraries
dnl ***************************************************************************

old_LIBS=$LIBS
AC_CACHE_CHECK(for TCP wrapper library,
                blb_cv_c_lwrap,

for libwrap in "-lwrap" "/usr/local/lib/libwrap.a"; do
        LIBS="$old_LIBS $libwrap"

        [AC_TRY_LINK(,
        [
}

int allow_severity;
int deny_severity;
extern int hosts_access(void);

int foo(void)
{
        hosts_access();
],
        [blb_cv_c_lwrap=$libwrap
        break],
        blb_cv_c_lwrap="")
done

])
LIBS=$old_LIBS
LIBWRAP_LIBS=$blb_cv_c_lwrap

if test "x$enable_tcp_wrapper" = "xauto"; then
	AC_MSG_CHECKING(whether to enable TCP wrapper support)
        if test "x$ac_cv_header_tcpd_h" = "xyes" -a "x$blb_cv_c_lwrap" != "x"; then
                enable_tcp_wrapper=yes
                AC_MSG_RESULT(yes)
        else
                LIBWRAP_LIBS=""
                AC_MSG_RESULT(no)
                enable_tcp_wrapper=no
        fi
elif test "x$enable_tcp_wrapper" != "xyes"; then
        LIBWRAP_LIBS=""
fi

dnl ***************************************************************************
dnl -ldl
dnl ***************************************************************************

AC_CHECK_LIB(dl, dlsym, DL_LIBS="-ldl")

dnl ***************************************************************************
dnl libdbi headers/libraries
dnl ***************************************************************************
if test "x$enable_sql" = "xyes" || test "x$enable_sql" = "xauto"; then
	PKG_CHECK_MODULES(LIBDBI, dbi >= $LIBDBI_MIN_VERSION, with_libdbi="1", with_libdbi="0")
	if test "$with_libdbi" -eq 0; then
		dnl if libdbi has no .pc file (e.g., Ubuntu Precise), try it without one
		AC_CHECK_LIB(dbi, dbi_initialize_r, LIBDBI_LIBS="-ldbi"; LIBDBI_CFLAGS="-I/usr/include/dbi"; with_libdbi="1")
	fi

	AC_MSG_CHECKING(whether to enable SQL support)
	if test "$with_libdbi" -eq 1; then
		enable_sql="yes"
		AC_MSG_RESULT(yes)
	elif test "x$enable_sql" = "xyes"; then
		AC_MSG_RESULT(no)
		enable_sql="no"
                AC_MSG_ERROR([Could not find libdbi, and SQL support was explicitly enabled.])
        else
		AC_MSG_RESULT(no)
	fi
fi

dnl ***************************************************************************
dnl GLib headers/libraries
dnl ***************************************************************************

GLIB_ADDONS="gmodule-2.0 gthread-2.0"
PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_MIN_VERSION $GLIB_ADDONS,,)

if test "$linking_mode" != "dynamic"; then
	# strip out -ldl & -lrt as it cannot be linked statically
	GLIB_LIBS=`echo $GLIB_LIBS | tr ' ' '\n' | egrep -v "^(-ldld?)|(-lrt)$" | tr '\n' ' '`

	old_LIBS=$LIBS
	LIBS="$LD_START_STATIC $GLIB_LIBS $LD_END_STATIC $BASE_LIBS"
	AC_CHECK_FUNC(g_hash_table_new, blb_cv_static_glib=yes, blb_cv_static_glib=no)
	LIBS=$old_LIBS
fi

GLIB_CFLAGS="${GLIB_CFLAGS} -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_32 -DGLIB_VERSION_MAX_ALLOWED=0x023C00"

old_CPPFLAGS=$CPPFLAGS
CPPFLAGS="$GLIB_CFLAGS"
old_LDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS $GLIB_LIBS"

old_LIBS=$LIBS
LIBS="$LIBS $GLIB_LIBS"
AC_CHECK_FUNCS(g_list_copy_deep \
  g_queue_free_full \
  g_list_free_full \
  g_ptr_array_find_with_equal_func \
  g_canonicalize_filename \
  g_hash_table_contains)
LIBS=$old_LIBS

AC_CACHE_CHECK(sanity checking Glib headers,
               blb_cv_glib_sane,
[AC_TRY_RUN([
#include <glib.h>

int main()
{
  if (sizeof(long) != GLIB_SIZEOF_LONG)
    return 1;
  return 0;
}
],
blb_cv_glib_sane=yes,
blb_cv_glib_sane=no,
blb_cv_glib_sane=yes)])
CPPFLAGS=$old_CPPFLAGS
LDFLAGS=$old_LDFLAGS

if test "x$blb_cv_glib_sane" = "xno"; then
	AC_MSG_ERROR([Glib headers inconsistent with current compiler setting. You might be using 32 bit Glib with a 64 bit compiler, check PKG_CONFIG_PATH])
fi

if test "x$linking_mode" != "xdynamic" -a "x$blb_cv_static_glib" = "xno"; then
	AC_MSG_ERROR([static GLib libraries not found (a file named libglib-2.0.a), either link GLib dynamically using the --enable-dynamic-linking or install a static GLib])
fi

dnl ***************************************************************************
dnl geoip2 headers/libraries
dnl ***************************************************************************
if test "x$enable_geoip2" = "xyes" || test "x$enable_geoip2" = "xauto"; then
        if test "x$MAXMINDDB_LIBS" != "x"; then
                AC_MSG_CHECKING([for MAXMINDDB])
                AC_MSG_RESULT([yes (MAXMINDDB_LIBS set, will use that.)])
                with_maxminddb="yes"
        else
                AC_MSG_CHECKING([for MAXMINDDB])
                AC_CHECK_LIB(maxminddb, MMDB_open, MAXMINDDB_LIBS="-lmaxminddb"; with_maxminddb="yes", with_maxminddb="no")
                AC_SUBST(MAXMINDDB_LIBS)
        fi

        if test "x$with_maxminddb" = "xno" && test "x$enable_geoip2" = "xyes"; then
                AC_MSG_ERROR([Could not find libmaxminddb, and geoip2 support was explicitly enabled.])
        fi

        if test "x$with_maxminddb" = "xyes"; then
                AC_CONFIG_LINKS([modules/geoip2/tests/test.mmdb:modules/geoip2/tests/test.mmdb])
        fi

        enable_geoip2="$with_maxminddb"
fi

dnl ***************************************************************************
dnl pcre headers/libraries
dnl ***************************************************************************

if test "x$linking_mode" = "xmixed"; then

	# check if we have a pcre bundled in glib. In case glib is
        # dynamic it doesn't matter as glib doesn't export those
        # symbols.  But in case glib is static, linking it through
        # glib and through libpcre would clash.
        old_LIBS="$LIBS"
        LIBS="$LD_START_STATIC $GLIB_LIBS $LD_END_STATIC $LIBS"
        AC_CHECK_FUNC(pcre_compile2, AC_MSG_ERROR([You cannot use a GLib embedded PCRE in mixed linking mode]))
        LIBS="$old_LIBS"
fi

PKG_CHECK_MODULES(PCRE, libpcre >= $PCRE_MIN_VERSION,, PCRE_LIBS="")
if test -z "$PCRE_LIBS"; then
	AC_MSG_ERROR(Cannot find pcre version >= $PCRE_MIN_VERSION it is a hard dependency from syslog-ng 3.6 onwards)
fi

dnl ***************************************************************************
dnl OpenSSL headers/libraries
dnl ***************************************************************************

# openssl is needed for:
#  * TLS support

dnl check OpenSSL static linking
PKG_CHECK_MODULES(OPENSSL,
                  openssl >= $OPENSSL_MIN_VERSION,,
                  AC_MSG_ERROR(Cannot find OpenSSL libraries with version >= $OPENSSL_MIN_VERSION it is a hard dependency from syslog-ng 3.7 onwards))

if test -n "$OPENSSL_LIBS" -a "$linking_mode" != "dynamic"; then
        dnl required for openssl, but only when linking statically
        AC_CHECK_LIB(z, inflate, ZLIB_LIBS="-lz")

	dnl Remove -ldl as it cannot be linked statically on some platforms, it'll be present in DL_LIBS
	OPENSSL_LIBS=`echo $OPENSSL_LIBS | tr ' ' '\n' | egrep -v "^-ldld?$" | tr '\n' ' '`

	old_LIBS=$LIBS
	LIBS="$LD_START_STATIC $OPENSSL_LIBS $ZLIB_LIBS $LD_END_STATIC $DL_LIBS"
	AC_CHECK_FUNC(SSL_library_init, blb_cv_static_openssl=yes, blb_cv_static_openssl=no)
	LIBS=$old_LIBS
fi

CPPFLAGS_SAVE="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $OPENSSL_CFLAGS"

AC_CHECK_DECLS([SSL_CTX_get0_param],[], [], [[#include <openssl/ssl.h>]])
AC_CHECK_DECLS([X509_STORE_CTX_get0_cert],[], [], [[#include <openssl/ssl.h>]])
AC_CHECK_DECLS([X509_get_extension_flags], [], [], [[#include <openssl/x509v3.h>]])
AC_CHECK_DECLS([EVP_MD_CTX_reset], [], [], [[#include <openssl/evp.h>]])
AC_CHECK_DECLS([ASN1_STRING_get0_data], [], [], [[#include <openssl/asn1.h>]])
AC_CHECK_DECLS([DH_set0_pqg], [], [], [[#include <openssl/dh.h>]])
AC_CHECK_DECLS([BN_get_rfc3526_prime_2048], [], [], [[#include <openssl/bn.h>]])
AC_CHECK_DECLS([SSL_CTX_set_num_tickets],[], [], [[#include <openssl/ssl.h>]])

CPPFLAGS="$CPPFLAGS_SAVE"

dnl
dnl Right now, openssl is never linked statically as it is only used by the
dnl TLS build of the afsocket plugin which is loaded dynamically anyway.
dnl
dnl The static check remains though, because the core may need openssl in the
dnl future, in which case it becomes relevant again.
dnl

dnl if test "x$linking_mode" != "xdynamic" -a "x$blb_cv_static_openssl" = "xno"; then
dnl	AC_MSG_ERROR([static OpenSSL libraries not found (libssl.a, libcrypto.a and their external dependencies like libz.a), either link OpenSSL statically using the --enable-dynamic-linking, or install a static OpenSSL])
dnl fi

dnl ***************************************************************************
dnl libnet headers/libraries
dnl ***************************************************************************
AC_MSG_CHECKING(for LIBNET)
if test "x$with_libnet" = "x"; then
        LIBNET_CONFIG="`which libnet-config`"
else
        LIBNET_CONFIG="$with_libnet/libnet-config"
fi

if test -n "$LIBNET_CONFIG" -a -x "$LIBNET_CONFIG"; then
        LIBNET_CFLAGS="`$LIBNET_CONFIG --defines`"
        LIBNET_LIBS="`$LIBNET_CONFIG --libs`"
        AC_MSG_RESULT(yes)
dnl libnet-config does not provide the _DEFAULT_SOURCE define, that can cause warning during build
dnl as upstream libnet-config does uses _DEFAULT_SOURCE this is just a fix till
        LIBNET_CFLAGS="$LIBNET_CFLAGS -D_DEFAULT_SOURCE"

else
        LIBNET_LIBS=
        AC_MSG_RESULT(no)
fi


if test "x$enable_spoof_source" = "xauto"; then
	AC_MSG_CHECKING(whether to enable spoof source support)
        if test "x$LIBNET_LIBS" != "x"; then
                enable_spoof_source=yes
                AC_MSG_RESULT(yes)
	else
		enable_spoof_source=no
                LIBNET_LIBS=
		LIBNET_CFLAGS=
		AC_MSG_RESULT(no)
        fi
elif test "x$enable_spoof_source" = "xyes"; then
  if test "x$LIBNET_LIBS" = "x"; then
    AC_MSG_ERROR([Could not find libnet, and spoof source support was explicitly enabled.])
  fi
elif test "x$enable_spoof_source" = "xno"; then
	LIBNET_CFLAGS=""
	LIBNET_LIBS=""
	enable_spoof_source=no
else
  AC_MSG_ERROR([Invalid value ($enable_spoof_source) for enable-spoof-source])
fi

dnl ***************************************************************************
dnl afsnmp module & net-snmp headers/libraries
dnl ***************************************************************************
AC_MSG_CHECKING(whether to enable snmp destination driver)

if test "x$with_net_snmp" = "x"; then
   NETSNMP_CONFIG="`which net-snmp-config`"
else
   NETSNMP_CONFIG="$with_net_snmp/net-snmp-config"
fi

if test "x$enable_afsnmp" = "xyes"; then
   if test -n "$NETSNMP_CONFIG" -a -x "$NETSNMP_CONFIG"; then
     enable_afsnmp="yes"
     AC_MSG_RESULT(yes)
   else
     enable_afsnmp_dest="no"
     AC_MSG_RESULT(no)
     AC_MSG_ERROR("AFSNMP module is explicit enabled but no net-snmp lib found")
   fi
elif test "x$enable_afsnmp" = "xauto"; then
  if test -n "$NETSNMP_CONFIG" -a -x "$NETSNMP_CONFIG"; then
     enable_afsnmp="yes"
     AC_MSG_RESULT(yes)
  else
     enable_afsnmp="no"
     AC_MSG_RESULT(no)
  fi
else
  enable_afsnmp="no"
  AC_MSG_RESULT(no)
fi

if test "$enable_afsnmp" = "yes"; then
  NETSNMP_CFLAGS="`$NETSNMP_CONFIG --cflags`"
  NETSNMP_LIBS="`$NETSNMP_CONFIG --libs`"
else
  NETSNMP_CFLAGS=
  NETSNMP_LIBS=
fi

dnl ***************************************************************************
dnl Criterion headers/libraries
dnl ***************************************************************************

PKG_CHECK_MODULES(CRITERION, criterion >= $CRITERION_MIN_VERSION,
  [with_criterion="system"],
  [AC_MSG_WARN([pkg-config was not able to find Criterion >= $CRITERION_MIN_VERSION]); with_criterion="no"])

if test "$with_criterion" != "no"; then
   enable_tests=yes
else
  enable_tests=no
fi

dnl ***************************************************************************
dnl ivykis headers/libraries
dnl ***************************************************************************

IVYKIS_INTERNAL_SOURCE_EXISTS="test -f $srcdir/lib/ivykis/src/iv_main_posix.c"

if $IVYKIS_INTERNAL_SOURCE_EXISTS; then
	AC_CONFIG_SUBDIRS([lib/ivykis])
	IVYKIS_SUBDIRS=lib/ivykis
fi


INTERNAL_IVYKIS_CFLAGS=""
if test "x$with_ivykis" = "xinternal"; then
	if $IVYKIS_INTERNAL_SOURCE_EXISTS; then
		# these can only be used in lib as it assumes
		# the current directory just one below ivykis

		IVYKIS_LIBS="-Wl,${WHOLE_ARCHIVE_OPT} -L\$(top_builddir)/lib/ivykis/src -livykis -Wl,${NO_WHOLE_ARCHIVE_OPT}"
		IVYKIS_CFLAGS="-isystem \$(top_srcdir)/lib/ivykis/src/include -isystem \$(top_builddir)/lib/ivykis/src/include"
                INTERNAL_IVYKIS_CFLAGS="-isystem \${includedir}/syslog-ng/ivykis"

		IVYKIS_VERSION_UPDATED="yes"

		# LIBS to use when libtool is not applicable (when linking the main syslog-ng executable in mixed linking mode)
		IVYKIS_NO_LIBTOOL_LIBS="-Wl,${WHOLE_ARCHIVE_OPT} -L\$(top_builddir)/lib/ivykis/src/.libs -livykis -Wl,${NO_WHOLE_ARCHIVE_OPT}"
	else
		AC_MSG_ERROR([Internal ivykis sources not found in lib/ivykis. This is a hard dependency, unable to build syslog-ng without them.])
	fi
else
	with_ivykis="system"
	PKG_CHECK_MODULES(IVYKIS, ivykis >= $IVYKIS_MIN_VERSION,,)
	PKG_CHECK_MODULES(IVYKIS_VERSION_CHECK, ivykis >= $IVYKIS_UPDATED_VERSION, [IVYKIS_VERSION_UPDATED="yes"], [IVYKIS_VERSION_UPDATED="no"])

	# in case we're using a system installed ivykis, we can link against
	# it even without libtool and without extra linker arguments (as
	# we're linking dynamically)

	IVYKIS_NO_LIBTOOL_LIBS="$IVYKIS_LIBS"
fi

dnl ***************************************************************************
dnl json headers/libraries
dnl ***************************************************************************

enable_json="no"

JSON_INTERNAL_SOURCE_EXISTS="test -f $srcdir/lib/jsonc/json.h"

if $JSON_INTERNAL_SOURCE_EXISTS; then
  AC_CONFIG_SUBDIRS([lib/jsonc])
  JSON_SUBDIRS="lib/jsonc"
fi

if test "x$with_jsonc" = "xsystem" -o "x$with_jsonc" = "xauto"; then
   enable_json="yes"
   PKG_CHECK_EXISTS(json-c, json_module_name="json-c",
   PKG_CHECK_EXISTS(json, json_module_name="json"))
   PKG_CHECK_MODULES(JSON, $json_module_name >= $JSON_C_MIN_VERSION,[with_jsonc="system"], [JSON_LIBS=""; enable_json="no"])
   if test "x$with_jsonc" = "xsystem" -a "x$enable_json" = "xno"; then
     AC_MSG_ERROR([json-c library development files cannot be not found on system!])
   fi
fi
if test "x$with_jsonc" = "xinternal" -o "x$with_jsonc" = "xauto"; then
  AC_MSG_CHECKING(for JSON)
  if $JSON_INTERNAL_SOURCE_EXISTS; then
      JSON_LIBS="\$(top_builddir)/lib/jsonc/libjson-c.la"
      JSON_DEPENDENCY="$JSON_LIBS"
      JSON_CFLAGS="-I\$(top_srcdir)/lib/jsonc -I\$(top_builddir)/lib/jsonc"
      enable_json="yes"
      with_jsonc="internal"
  else
      AC_MSG_WARN([Internal jsonc sources not found in lib/jsonc])
      enable_json="no"
      with_jsonc="no"
  fi
  AC_MSG_RESULT($enable_json)
fi

dnl ***************************************************************************
dnl mongo-c-driver headers/libraries
dnl ***************************************************************************

PKG_CHECK_MODULES(LIBMONGO, libmongoc-1.0 >= $LMC_MIN_VERSION, with_mongoc="yes", with_mongoc="no")

if test "x$with_mongoc" = "xno" && test "x$enable_mongodb" = "xyes"; then
   AC_MSG_ERROR([Could not find mongo-c-driver, and MongoDB support was explicitly enabled.])
fi

if test "x$with_mongoc" = "xyes"; then
CPPFLAGS_SAVE="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $LIBMONGO_CFLAGS"

AC_CHECK_DECLS([mongoc_uri_set_option_as_int32], [], [], [[#include <mongoc.h>]])

CPPFLAGS="$CPPFLAGS_SAVE"
fi

dnl ***************************************************************************
dnl libesmtp headers/libraries
dnl ***************************************************************************
if test "x$enable_smtp" != "xno" && test "x$with_libesmtp" != "xno"; then
	libesmtp="yes"
	if test "x$with_libesmtp" != "xyes" && test "x$with_libesmtp" != "x"; then
		CPPFLAGS_SAVE="$CPPFLAGS"
		LDFLAGS_SAVE="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS -I$with_libesmtp/include"
		LDFLAGS="$LDFLAGS -L$with_libesmtp/lib"
		AC_CHECK_HEADER(libesmtp.h, [LIBESMTP_CFLAGS="-I$with_libesmtp/include"
					     LIBESMTP_LIBS="-L$with_libesmtp/lib -lesmtp"], [libesmtp=no])
		CPPFLAGS="$CPPFLAGS_SAVE"
		LDFLAGS="$LDFLAGS_SAVE"
	else
		PKG_CHECK_MODULES(LIBESMTP, libesmtp-1.0, libesmtp="yes", libesmtp="no")
		if test "x$libesmtp" = "xno"; then
			AC_MSG_CHECKING(for libESMTP with libesmtp-config)
			if libesmtp-config --version >/dev/null 2>&1; then
				AC_MSG_RESULT(yes)
				LIBESMTP_CFLAGS="`libesmtp-config --cflags`"
				LIBESMTP_LIBS="`libesmtp-config --libs`"
				libesmtp="yes"
			else
				AC_MSG_RESULT(no)
				libesmtp=no
			fi
                fi
	fi
	if test "x$enable_smtp" = "xyes" && test "x$libesmtp" = "xno"; then
		AC_MSG_ERROR(libESMTP not found)
	fi
	enable_smtp=$libesmtp
fi


dnl ***************************************************************************
dnl libmqtt headers/libraries
dnl ***************************************************************************

if test "x$enable_mqtt" = "xyes" || test "x$enable_mqtt" = "xauto"; then
	if test "x$with_libpaho_mqtt" != "x"; then
		CPPFLAGS_SAVE="$CPPFLAGS"
		LDFLAGS_SAVE="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS -I$with_libpaho_mqtt/include"
		LDFLAGS="$LDFLAGS -L$with_libpaho_mqtt -lpaho-mqtt3c"
		AC_CHECK_HEADER(MQTTClient.h, [LIBPAHO_MQTT_CFLAGS="-I$with_libpaho_mqtt/include"
						LIBPAHO_MQTT_LIBS="-L$with_libpaho_mqtt/lib -lpaho-mqtt3c"
						libpaho_mqtt="yes"], [libpaho_mqtt=no])

		CPPFLAGS="$CPPFLAGS_SAVE"
		LDFLAGS="$LDFLAGS_SAVE"
	else
		AC_CHECK_LIB(paho-mqtt3c, MQTTClient_create, LIBPAHO_MQTT_LIBS="-lpaho-mqtt3c"; libpaho_mqtt="yes", libpaho_mqtt="no")
	fi
	if test "x$enable_mqtt" = "xyes" && test "x$libpaho_mqtt" = "xno"; then
		AC_MSG_ERROR(libPAHO_MQTT not found)
	fi
	enable_mqtt=$libpaho_mqtt
fi

dnl ***************************************************************************
dnl libcurl headers/libraries
dnl ***************************************************************************
if test "x$enable_http" != "xno" && test "x$with_libcurl" != "xno"; then
	libcurl="yes"
	if test "x$with_libcurl" != "xyes" && test "x$with_libcurl" != "x"; then
		CPPFLAGS_SAVE="$CPPFLAGS"
		LDFLAGS_SAVE="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS -I$with_libcurl/include"
		LDFLAGS="$LDFLAGS -L$with_libcurl/lib"
		AC_CHECK_HEADER(curl/curl.h, [LIBCURL_CFLAGS="-I$with_libcurl/include"
					     LIBCURL_LIBS="-L$with_libcurl/lib -lcurl"], [libcurl=no])
		CPPFLAGS="$CPPFLAGS_SAVE"
		LDFLAGS="$LDFLAGS_SAVE"
	else
		AC_MSG_CHECKING(for libcurl)
		if curl-config --version >/dev/null 2>&1; then
			AC_MSG_RESULT(yes)
			LIBCURL_CFLAGS="`curl-config --cflags`"
			LIBCURL_LIBS="`curl-config --libs`"
		else
			AC_MSG_RESULT(no)
			libcurl=no
		fi
	fi
	if test "x$enable_http" = "xyes" && test "x$libcurl" = "xno"; then
		AC_MSG_ERROR(libcurl not found)
	fi
	enable_http=$libcurl

        if test "$enable_http" = "yes"; then
           old_CFLAGS=$CFLAGS
           CFLAGS=$LIBCURL_CFLAGS
           AC_CHECK_DECLS([CURL_SSLVERSION_TLSv1_0, CURL_SSLVERSION_TLSv1_1, CURL_SSLVERSION_TLSv1_2, CURL_SSLVERSION_TLSv1_3],
                          [], [],
                          [[#include <curl/curl.h>]])
           CFLAGS=$old_CFLAGS
        fi
fi

dnl ***************************************************************************
dnl libhiredis headers/libraries
dnl ***************************************************************************
if test "x$enable_redis" != "xno" && test "x$with_redis" != "xno"; then
       hiredis="yes"
       if test "x$with_libhiredis" != "xyes" && test "x$with_libhiredis" != "x"; then
               CFLAGS_SAVE="$CFLAGS"
               LDFLAGS_SAVE="$LDFLAGS"
               CFLAGS="$CFLAGS -I$with_libhiredis/include"
               LDFLAGS="$LDFLAGS -L$with_libhiredis/lib"
               AC_CHECK_HEADER(hiredis/hiredis.h, [HIREDIS_CFLAGS="-I$with_libhiredis/include"
                                                   HIREDIS_LIBS="-L$with_libhiredis/lib -lhiredis"], [hiredis=no])
               CFLAGS="$CFLAGS_SAVE"
               LDFLAGS="$LDFLAGS_SAVE"
       else
               hiredis="yes"
               PKG_CHECK_MODULES(HIREDIS, hiredis >= $HIREDIS_MIN_VERSION, ,
                                 [AC_MSG_WARN([pkg-config was not able to find hiredis >= $HIREDIS_MIN_VERSION])
				  PKG_CHECK_MODULES(HIREDIS, libhiredis >= $HIREDIS_MIN_VERSION,,
				  [AC_MSG_WARN([pkg-config was not able to find libhiredis >= $HIREDIS_MIN_VERSION])
				   hiredis=no])])
       fi

       if test "x$enable_redis" = "xyes" && test "x$hiredis" = "xno"; then
               AC_MSG_ERROR(libHIREDIS not found)
       fi

       enable_redis=$hiredis
fi

dnl ***************************************************************************
dnl rabbitmq-c headers/libraries
dnl ***************************************************************************

PKG_CHECK_MODULES(LIBRABBITMQ, librabbitmq >= $LIBRABBITMQ_MIN_VERSION, with_librabbitmq_client="yes", with_librabbitmq_client="no")

if test "$enable_amqp" = "yes" && test "$with_librabbitmq_client" = "no"; then
       AC_MSG_ERROR([Could not find librabbitmq-c, and AMQP support was explicitly enabled.])
fi

old_LIBS=$LIBS
LIBS=$LIBRABBITMQ_LIBS
AC_CHECK_FUNCS(amqp_ssl_socket_set_verify_peer)
LIBS=$old_LIBS

dnl ***************************************************************************
dnl rdkafka headers/libraries
dnl ***************************************************************************

PKG_CHECK_MODULES(LIBRDKAFKA, rdkafka >= $LIBRDKAFKA_MIN_VERSION, with_librdkafka="yes", with_librdkafka="no")

if test "$enable_kafka" = "yes" && test "$with_librdkafka" = "no"; then
       AC_MSG_ERROR([Could not find librdkafka, and Kafka support was explicitly enabled.])
fi

dnl
dnl Check if librdkafka has transactional api
dnl

old_LIBS=$LIBS
old_CFLAGS=$CFLAGS
LIBS=$LIBRDKAFKA_LIBS
CFLAGS=$LIBRDKAFKA_CFLAGS
AC_CHECK_FUNCS(rd_kafka_init_transactions)
LIBS=$old_LIBS
CFLAGS=$old_CFLAGS


if test "x$enable_native" = "xyes" -o "x$enable_native" = "xauto"; then
    AC_CONFIG_FILES([syslog-ng-native-connector.pc])
fi

dnl ***************************************************************************
dnl riemann-client headers/libraries
dnl ***************************************************************************
if test "$enable_riemann" != "no"; then
 PKG_CHECK_MODULES(RIEMANN_CLIENT, riemann-client >= $LRC_MIN_VERSION,,riemann_found="no")
 if test "$riemann_found" = "no" && test "$enable_riemann" = "yes"; then
   AC_MSG_ERROR([Dependency for Riemann not found!])
 fi
 if test "$riemann_found" = "no"; then
   enable_riemann="no";
 else
   enable_riemann="yes";
 fi

 if test "$enable_riemann" = "yes"; then

    old_CFLAGS=$CFLAGS
    CFLAGS=`pkg-config --cflags riemann-client`
    AC_CHECK_DECLS(RIEMANN_EVENT_FIELD_TIME_MICROS,
                  AC_DEFINE(HAVE_RIEMANN_MICROSECONDS, 1, [Riemann microseconds support]),,
                  [[#include <riemann/event.h>]])
    CFLAGS=$old_CFLAGS
 fi
fi

dnl ***************************************************************************
dnl libsystemd headers/libraries
dnl ***************************************************************************

PKG_CHECK_MODULES(libsystemd, libsystemd >= ${LIBSYSTEMD_MIN_VERSION},
                  have_libsystemd="yes", have_libsystemd="no")

PKG_CHECK_MODULES(libsystemd_namespaces, libsystemd >= ${LIBSYSTEMD_WITH_JOURNAL_NAMESPACES_MIN_VERSION},
                  journal_namespaces="yes", journal_namespaces="no")

dnl ***************************************************************************
dnl python checks
dnl ***************************************************************************
if test "x$enable_python" != "xno"; then
  if test "x$with_python" = "xauto"; then
    PKG_CHECK_MODULES(PYTHON, python3-embed, python_found="yes", [
      PKG_CHECK_MODULES(PYTHON, python3, python_found="yes", python_found="no")
    ])
    if test "$python_found" = "no"; then
      PKG_CHECK_MODULES(PYTHON, python2, python_found="yes", python_found="no")
      if test "$python_found" = "no"; then
        PKG_CHECK_MODULES(PYTHON, python, python_found="yes", python_found="no")
      fi
    fi
  else
    case "$with_python" in
      [[0-9]])
          with_python="python${with_python}"
          ;;
      [[0-9]].[[0-9]])
          with_python="python-${with_python}"
          ;;
      [[0-9]].[[0-9][0-9]])
          with_python="python-${with_python}"
          ;;
    esac

    PKG_CHECK_MODULES(PYTHON, $with_python-embed, python_found="yes", [
      PKG_CHECK_MODULES(PYTHON, $with_python, python_found="yes", python_found="no")
    ])
  fi

  if test "x$python_found" = "xyes"; then
    old_LIBS=$LIBS
    LIBS="$LIBS $PYTHON_LIBS"
    AC_CHECK_FUNCS(PyUnicode_AsUTF8, with_python=python3, with_python=python2)
    LIBS=$old_LIBS
  fi

  if test "x$enable_python" = "xyes" -a "x$python_found" = "xno"; then
    AC_MSG_ERROR([Could not find the requested Python development libraries])
  fi
  enable_python="$python_found"

  if (echo "$with_python" | grep -Eq "python-?3.*"); then
    python_version=3
  else
    python_version=2
  fi
else
  with_python=""
fi

dnl ***************************************************************************
dnl pylint
dnl ***************************************************************************

if test "x$with_pylint" != "x"; then
    PYLINT=$with_pylint
else
    if test "$python_found" = "yes"; then
      if test "$python_version" = "3"; then
          AC_CHECK_PROG(PYLINT, pylint3, pylint3)
          if test "x$PYLINT" == "x"; then
            AC_CHECK_PROG(PYLINT, pylint, pylint)
          fi
      else
          AC_CHECK_PROG(PYLINT, pylint, pylint)
      fi
    fi
fi

dnl ***************************************************************************
dnl docbook
dnl ***************************************************************************

if test "x$enable_manpages" != "xno"; then
    if test "x$with_docbook" = "xauto"; then
        XSL_STYLESHEET=`find /usr/share/{xml,sgml} -path '*/manpages/docbook.xsl' 2>/dev/null | head -n 1`
        if test -z "$XSL_STYLESHEET"; then
            XSL_STYLESHEET="http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl"
        fi
    else
        XSL_STYLESHEET=$with_docbook
    fi
fi

if test "x$enable_manpages_install" = "xauto"; then
    dnl do we have the generated manpages (part of the tarball but not in git)
    if test -f doc/man/syslog-ng.8 -o "x$enable_manpages" = "xyes"; then
        enable_manpages_install=yes
    else
        enable_manpages_install=no
    fi
fi

dnl ***************************************************************************
dnl java headers/libraries
dnl ***************************************************************************

if test "x$enable_java" = "xauto" || test "x$enable_java" = "xyes"; then
    if test "x$enable_java" = "xauto"; then
      AC_CHECK_JAVA_VERSION([$JAVA_MIN_VERSION], [enable_java=yes], [enable_java=no])
    else
      AC_CHECK_JAVA_VERSION([$JAVA_MIN_VERSION], [enable_java=yes], [AC_MSG_ERROR([Java not found])])
    fi
fi

if test "x$enable_java" = "xyes"; then
    if test "x$enable_java_modules" = "xauto" || test "x$enable_java_modules" = "xyes"; then
        if test "x$enable_java_modules" = "xauto"; then
          AC_CHECK_GRADLE_VERSION([$GRADLE_MIN_VERSION], [enable_java_modules=yes], [enable_java_modules=no])
        else
          AC_CHECK_GRADLE_VERSION([$GRADLE_MIN_VERSION], [enable_java_modules=yes], [AC_MSG_ERROR([Gradle not found])])
        fi
    fi
else
    if test "x$enable_java_modules" = "xyes"; then
        AC_MSG_ERROR([Could not build Java modules without Java])
    elif test "x$enable_java_modules" = "xauto"; then
        AC_MSG_WARN([Could not build Java modules without Java])
	enable_java_modules=no
    fi
fi

dnl ***************************************************************************
dnl misc features to be enabled
dnl ***************************************************************************

if test "x$ac_cv_lib_door_door_create" = "xyes"; then
        AC_CHECK_HEADERS(pthread.h)
        AC_CHECK_LIB(pthread, pthread_create)
fi

AC_MSG_CHECKING(whether to enable Sun STREAMS support)
if test "x$ac_cv_header_stropts_h" = "xyes" -a \
        "x$ac_cv_header_sys_strlog_h" = "xyes" -a \
        "x$enable_sun_streams" != "xno" -a \
        "x$blb_cv_c_i_conslog" != "xno"; then
        enable_sun_streams=yes
        AC_MSG_RESULT(yes)
else
        enable_sun_streams=no
        AC_MSG_RESULT(no)
fi

AC_MSG_CHECKING(whether to enable OpenBSD system source support)
if test "x$enable_openbsd_system_source" = "xyes"; then
        enable_openbsd_system_source="yes"
        AC_MSG_RESULT(yes)
elif test "x$enable_openbsd_system_source" = "xauto" -a \
        "$ostype" = "OpenBSD" ; then
        enable_openbsd_system_source="yes"
        AC_MSG_RESULT(yes)
else
        enable_openbsd_system_source="no"
        AC_MSG_RESULT(no)
fi

if test "x$enable_env_wrapper" = "xauto"; then
	if test "x$env_ld_library_path" != "x"; then
		enable_env_wrapper="yes"
	else
		enable_env_wrapper="no"
	fi
fi

if test "x$enable_ipv6" = "xauto"; then
	AC_MSG_CHECKING(whether to enable IPv6 support)
        if test "x$blb_cv_c_struct_sockaddr_in6" = "xyes"; then
                enable_ipv6=yes
                AC_MSG_RESULT(yes)
        else
                enable_ipv6=no
                AC_MSG_RESULT(no)
        fi
fi

AC_CHECK_DECLS([TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT], [], [],
                [[#include <sys/socket.h>
                  #include <netinet/in.h>
                  #include <netinet/tcp.h>]])

if test "x$ac_cv_have_decl_TCP_KEEPIDLE" = "xyes" -a "x$ac_cv_have_decl_TCP_KEEPINTVL" = "xyes" -a "x$ac_cv_have_decl_TCP_KEEPCNT" = "xyes"; then
        AC_DEFINE(HAVE_TCP_KEEPALIVE_TIMERS, 1, [TCP keepalive timers])
fi

if test "x$enable_linux_caps" = "xyes" -o "x$enable_linux_caps" = "xauto"; then
        PKG_CHECK_MODULES(LIBCAP, libcap, has_linux_caps="yes", has_linux_caps="no")

        if test "x$has_linux_caps" = "xno"; then
                if test "x$ac_cv_header_sys_capability_h" = "xyes"; then
                        AC_CHECK_LIB(cap, cap_set_proc, LIBCAP_LIBS="-lcap"; has_linux_caps="yes", has_linux_caps="no")
                else
                        has_linux_caps="no"
                fi
                AC_MSG_CHECKING(whether to enable Linux capability support)
                AC_MSG_RESULT([$has_linux_caps])
        fi

        if test "x$enable_linux_caps" = "xyes" -a "x$has_linux_caps" = "xno"; then
           AC_MSG_ERROR([Cannot enable Linux capability support.])
        fi

        enable_linux_caps="$has_linux_caps"
fi

if test "x$enable_mongodb" = "xauto"; then
	AC_MSG_CHECKING(whether to enable mongodb destination support)
	if test "x$with_mongoc" != "xno"; then
		enable_mongodb="yes"
	else
		enable_mongodb="no"
	fi
	AC_MSG_RESULT([$enable_mongodb])
fi

if test "x$enable_amqp" = "xauto"; then
    AC_MSG_CHECKING(whether to enable amqp destination support)
    if test "x$with_librabbitmq_client" != "xno"; then
        enable_amqp="yes"
    else
        enable_amqp="no"
    fi
    AC_MSG_RESULT([$enable_amqp])
fi

if test "x$enable_kafka" = "xauto"; then
    AC_MSG_CHECKING(whether to enable kafka support)
    if test "x$with_librdkafka" != "xno"; then
        enable_kafka="yes"
    else
        enable_kafka="no"
    fi
    AC_MSG_RESULT([$enable_kafka])
fi

if test "x$enable_systemd" = "xauto"; then
	if test "$ostype" = "Linux" -a "$have_libsystemd" = "yes"; then
		enable_systemd=yes
	else
		enable_systemd=no
	fi

fi

if test "x$enable_systemd" != "xyes"; then
  if test "x$with_systemd_journal" = "xauto"; then
    with_systemd_journal=no
  fi
fi

if test "x$enable_systemd" = "xyes"; then

        if test "x$with_systemdsystemunitdir" = "xyes"; then
		# no arguments, just --with-systemdsystemunitdir
		systemdsystemunitdir=`$PKG_CONFIG --variable=systemdsystemunitdir systemd`
		if test "$systemdsystemunitdir" = ""; then
			AC_MSG_ERROR([Error autodetecting systemdsystemunitdir, systemd pkg-config file not found?])
		fi
	elif test "$systemdsystemunitdir" = "no"; then
		# --without-systemdsystemunitdir was specified
		systemdsystemunitdir=""
	else
		systemdsystemunitdir="$with_systemdsystemunitdir"
	fi

        if test "x$have_libsystemd" = "xno"; then
	    PKG_CHECK_MODULES(libsystemd_daemon, libsystemd-daemon >= 31,enable_systemd="yes",enable_systemd="no")
            if test "x$with_systemd_journal" = "xauto"; then
                PKG_CHECK_MODULES(LIBSYSTEMD_JOURNAL, libsystemd-journal >= $JOURNALD_MIN_VERSION,
                                  with_systemd_journal=system, with_systemd_journal=optional)
            elif test "x$with_systemd_journal" = "xsystem"; then
                PKG_CHECK_MODULES(LIBSYSTEMD_JOURNAL, libsystemd-journal >= $JOURNALD_MIN_VERSION,,
                                  AC_MSG_ERROR([Detecting system related systemd-journal library failed]))
            fi
            libsystemd_CFLAGS="${LIBSYSTEMD_JOURNAL_CFLAGS} ${libsystemd_daemon_CFLAGS}"
            libsystemd_LIBS="${LIBSYSTEMD_JOURNAL_LIBS} ${libsystemd_daemon_LIBS}"
            AC_SUBST(libsystemd_CFLAGS)
            AC_SUBST(libsystemd_LIBS)
        else
            if test "x$with_systemd_journal" = "xauto"; then
                with_systemd_journal="system"
            fi
        fi
fi

dnl ***************************************************************************
dnl check if we have timezone variable in <time.h>
dnl ***************************************************************************
AC_VAR_TIMEZONE_EXTERNALS

dnl ***************************************************************************
dnl check fmemopen
dnl ***************************************************************************
AC_CHECK_FUNCS([fmemopen])

dnl ***************************************************************************
dnl sanitizer
dnl ***************************************************************************

if  test "x$with_sanitizer" != "xno"; then
   CFLAGS="$CFLAGS -O1 -fsanitize=$with_sanitizer -fno-omit-frame-pointer"
fi

dnl ***************************************************************************
dnl default modules to be loaded
dnl ***************************************************************************

### The default set of modules are those that provide functionality that
### were provided in syslog-ng 3.2 and prior, unless explicitly overridden
### by the user.
###
### New plugins can be loaded by explicit "@module foo" lines in the
### configuration file, or by the autoloading mechanism.

if test "x$module_path" = "x"; then
	module_path="$moduledir"
	java_module_path="$moduledir"/java-modules
fi

python_moduledir="$moduledir"/python

CPPFLAGS="$CPPFLAGS $GLIB_CFLAGS $EVTLOG_CFLAGS $PCRE_CFLAGS $OPENSSL_CFLAGS $LIBNET_CFLAGS $LIBDBI_CFLAGS $IVYKIS_CFLAGS $LIBCAP_CFLAGS -D_GNU_SOURCE -D_DEFAULT_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64"

########################################################
## NOTES: on how syslog-ng is linked
#
# There are two major linking modes currently:
#   1) dynamic: all libraries are linked dynamically, and assumed to be available as dynamic libraries
#
#   2) mixed: typical system provided libraries are linked dynamicaly, the
#      rest is linked statically (Glib etc).  Please note that _only_ the
#      main syslog-ng executable is linked this way so that it can be
#      started early during startup, tools and unit tests are linked
#      dynamically the same way.
#
# The following variables are introduced and AC_SUBSTed to be used in Makefiles:
#
# SYSLOGNG_DEPS_LIBS:
#       includes all dependendent libraries used by binary that can be linked in mixed mode (e.g. only the syslog-ng binary).
#
# TOOL_DEPS_LIBS:
#       executables (e.g. tools & unit test programs) that link against
#       libsyslog-ng.so should be linked with this set of libraries.  other
#       tools that do not use libsyslog-ng.so can use the _LIBS variables
#       directly.
#
# CORE_DEPS_LIBS:
#	libsyslog-ng.so is linked with this set of libraries.
#
# MODULE_DEPS_LIBS:
#       The set of libraries that modules should be linked against. Only to
#       satisfy their "core" dependency, any other libs that the core doesn't
#       depend on must be linked explicitly.
#
# MODULE_LDFLAGS:
#       The LDFLAGS to be passed when linking modules (may not contain
#       library references -l and such, only linker flags)
#
# Modules should be linked against libsyslog-ng.la and libraries that are
# _NOT_ linked into libsyslog-ng.la no need to define a LIBS variable for
# that.


if test -z "$MODULE_LDFLAGS"; then
	MODULE_LDFLAGS="-avoid-version -module -no-undefined"
fi
MODULE_DEPS_LIBS="\$(top_builddir)/lib/libsyslog-ng.la"

if test "x$linking_mode" = "xdynamic"; then
	SYSLOGNG_DEPS_LIBS="$LIBS $BASE_LIBS $GLIB_LIBS $EVTLOG_LIBS $SECRETSTORAGE_LIBS $RESOLV_LIBS $LIBCAP_LIBS $PCRE_LIBS $REGEX_LIBS $DL_LIBS"

	if test "x$with_ivykis" = "xinternal"; then
		# when using the internal ivykis, we're linking it statically into libsyslog-ng.so

		TOOL_DEPS_LIBS="$SYSLOGNG_DEPS_LIBS"
		CORE_DEPS_LIBS="$SYSLOGNG_DEPS_LIBS $IVYKIS_LIBS"
	else
		# otherwise everything needs to link against libivykis.so

		SYSLOGNG_DEPS_LIBS="$SYSLOGNG_DEPS_LIBS $IVYKIS_LIBS"

		TOOL_DEPS_LIBS="$SYSLOGNG_DEPS_LIBS"
		CORE_DEPS_LIBS="$SYSLOGNG_DEPS_LIBS"
	fi

	# syslog-ng binary is linked with the default link command (e.g. libtool)
	SYSLOGNG_LINK='$(LINK)'
else
	SYSLOGNG_DEPS_LIBS="$LIBS $BASE_LIBS $RESOLV_LIBS $EVTLOG_NO_LIBTOOL_LIBS $SECRETSTORAGE_NO_LIBTOOL_LIBS $LD_START_STATIC -Wl,${WHOLE_ARCHIVE_OPT} $GLIB_LIBS $PCRE_LIBS $REGEX_LIBS  -Wl,${NO_WHOLE_ARCHIVE_OPT} $IVYKIS_NO_LIBTOOL_LIBS $LD_END_STATIC $LIBCAP_LIBS $DL_LIBS"
	TOOL_DEPS_LIBS="$LIBS $BASE_LIBS $GLIB_LIBS $EVTLOG_LIBS $SECRETSTORAGE_LIBS $RESOLV_LIBS $LIBCAP_LIBS $PCRE_LIBS $REGEX_LIBS $IVYKIS_LIBS $DL_LIBS"
	CORE_DEPS_LIBS=""

	# bypass libtool in case we want to do mixed linking because it
	# doesn't support -Wl,-Bstatic -Wl,-Bdynamic on a per-library basis.
	SYSLOGNG_LINK='$(CCLD) $(AM_CFLAGS) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@'
fi
YFLAGS="-d"

enable_value()
{
        if test "x$1" = "xyes" ; then
                echo 1
        else
                echo 0
        fi
}

journald_mode()
{
  if test "x$with_systemd_journal" = "xno"; then
    echo SYSLOG_NG_JOURNALD_OFF
  elif test "x$with_systemd_journal" = "xsystem"; then
    echo SYSLOG_NG_JOURNALD_SYSTEM
  else
    echo SYSLOG_NG_JOURNALD_OPTIONAL
  fi
}

AC_DEFINE_UNQUOTED(JOURNALD_OFF, 0, ["Disable systemd-journal source"])
AC_DEFINE_UNQUOTED(JOURNALD_OPTIONAL, 1, ["Enable systemd-journal source if journald is available"])
AC_DEFINE_UNQUOTED(JOURNALD_SYSTEM, 2, ["Force systemd-journal source to use system's journald"])
AC_DEFINE_UNQUOTED(HAVE_JOURNAL_NAMESPACES, `(test "$journal_namespaces" = "yes" ) && echo 1 || echo 0`, [have journal namespaces])

AC_DEFINE_UNQUOTED(VERSION, "$PACKAGE_VERSION", [version number])
AC_DEFINE_UNQUOTED(SOURCE_REVISION, "$SOURCE_REVISION", [source revision])
AC_DEFINE_UNQUOTED(LICENSE_VERSION, "$LICENSE_VERSION", [Required license version])

AC_DEFINE_UNQUOTED(PATH_PREFIX, "$prefix", [prefix directory])
AC_DEFINE_UNQUOTED(PATH_EXECPREFIX, "$exec_prefix", [execprefix directory])

AC_DEFINE_UNQUOTED(PATH_SYSCONFDIR, "$sysconfdir", [sysconfdir])
AC_DEFINE_UNQUOTED(PATH_LOCALSTATEDIR, "$localstatedir", [local state directory])
AC_DEFINE_UNQUOTED(PATH_PIDFILEDIR, "$pidfiledir", [local state directory])
AC_DEFINE_UNQUOTED(PATH_DATAROOTDIR, "$datarootdir", [data root directory])
AC_DEFINE_UNQUOTED(PATH_DATADIR, "$datadir", [data directory])
AC_DEFINE_UNQUOTED(PATH_CONFIG_INCLUDEDIR, "$config_includedir", [include directory])
AC_DEFINE_UNQUOTED(PATH_SCLDIR, "$scldir", [SCL directory])
AC_DEFINE_UNQUOTED(PATH_XSDDIR, "$xsddir", [XSD directory])
AC_DEFINE_UNQUOTED(PATH_LIBEXECDIR, "$libexecdir", [libexec directory])
if test -n "$timezonedir"; then
        AC_DEFINE_UNQUOTED(PATH_TIMEZONEDIR, "$timezonedir", [timezone base directory])
fi
if test -n "$env_ld_library_path"; then
        AC_DEFINE_UNQUOTED(ENV_LD_LIBRARY_PATH, "$env_ld_library_path", [set LD_LIBRARY_PATH to this value])
fi
AC_DEFINE_UNQUOTED(PATH_MODULEDIR, "$moduledir", [module installation directory])
AC_DEFINE_UNQUOTED(PYTHON_MODULE_DIR, "$python_moduledir", [Python module installation directory])
AC_DEFINE_UNQUOTED(PYTHON3_HOME_DIR, "$python3_home_dir", [Hard-coded Python 3 home directory])
AC_DEFINE_UNQUOTED(PYTHON2_HOME_DIR, "$python2_home_dir", [Hard-coded Python 2 home directory])
AC_DEFINE_UNQUOTED(PATH_LOGGENPLUGINDIR, "$loggenplugindir", [loggenplugin installation directory])
AC_DEFINE_UNQUOTED(MODULE_PATH, "$module_path", [module search path])
AC_DEFINE_UNQUOTED(JAVA_MODULE_PATH, "$java_module_path", [java module search path])
AC_DEFINE_UNQUOTED(PATH_TOPSRC_DIR, "$abs_topsrcdir", [self-defined top_srcdir path])
AC_DEFINE_UNQUOTED(PACKAGE_NAME, "$PACKAGE_NAME", [package name])


AC_DEFINE_UNQUOTED(WITH_COMPILE_DATE, $wcmp_date, [Include the compile date in the binary])
AC_DEFINE_UNQUOTED(ENABLE_FORCED_SERVER_MODE, `enable_value $enable_forced_server_mode`, [Enable forced server mode])
AC_DEFINE_UNQUOTED(ENABLE_DEBUG, `enable_value $enable_debug`, [Enable debugging])
AC_DEFINE_UNQUOTED(ENABLE_GPROF, `enable_value $enable_gprof`, [Enable gcc profiling])
AC_DEFINE_UNQUOTED(ENABLE_MEMTRACE, `enable_value $enable_memtrace`, [Enable memtrace])
AC_DEFINE_UNQUOTED(ENABLE_SPOOF_SOURCE, `enable_value $enable_spoof_source`, [Enable spoof source support])
AC_DEFINE_UNQUOTED(ENABLE_IPV6, `enable_value $enable_ipv6`, [Enable IPv6 support])
AC_DEFINE_UNQUOTED(ENABLE_TCP_WRAPPER, `enable_value $enable_tcp_wrapper`, [Enable TCP wrapper support])
AC_DEFINE_UNQUOTED(ENABLE_LINUX_CAPS, `enable_value $enable_linux_caps`, [Enable Linux capability management support])
AC_DEFINE_UNQUOTED(ENABLE_ENV_WRAPPER, `enable_value $enable_env_wrapper`, [Enable environment wrapper support])
AC_DEFINE_UNQUOTED(ENABLE_SYSTEMD, `enable_value $enable_systemd`, [Enable systemd support])
AC_DEFINE_UNQUOTED(ENABLE_KAFKA, `enable_value $enable_kafka`, [Enable kafka support])
AC_DEFINE_UNQUOTED(SYSTEMD_JOURNAL_MODE, `journald_mode`, [Systemd-journal support mode])
AC_DEFINE_UNQUOTED(HAVE_INOTIFY, `enable_value $ac_cv_func_inotify_init`, [Have inotify])
AC_DEFINE_UNQUOTED(ENABLE_PYTHONv2, `(test "$python_version" = "2" ) && echo 1 || echo 0`, [Python2 c api])
AC_DEFINE_UNQUOTED(ENABLE_PYTHONv3, `(test "$python_version" = "3" ) && echo 1 || echo 0`, [Python3 c api])
AC_DEFINE_UNQUOTED(USE_CONST_IVYKIS_MOCK, `enable_value $IVYKIS_VERSION_UPDATED`, [ivykis version is greater than $IVYKIS_UPDATED_VERSION])

AM_CONDITIONAL(ENABLE_ENV_WRAPPER, [test "$enable_env_wrapper" = "yes"])
AM_CONDITIONAL(ENABLE_SYSTEMD, [test "$enable_systemd" = "yes"])
AM_CONDITIONAL(ENABLE_SYSTEMD_UNIT_INSTALL, [test "$systemdsystemunitdir" != ""])
AM_CONDITIONAL(ENABLE_SQL, [test "$enable_sql" = "yes"])
AM_CONDITIONAL(ENABLE_SUN_STREAMS, [test "$enable_sun_streams" = "yes"])
AM_CONDITIONAL(ENABLE_OPENBSD_SYSTEM_SOURCE, [test "$enable_openbsd_system_source" = "yes"])
AM_CONDITIONAL(ENABLE_PACCT, [test "$enable_pacct" = "yes"])
AM_CONDITIONAL(ENABLE_MONGODB, [test "$enable_mongodb" = "yes"])
AM_CONDITIONAL(ENABLE_SMTP, [test "$enable_smtp" = "yes"])
AM_CONDITIONAL(ENABLE_MQTT, [test "$enable_mqtt" = "yes"])
AM_CONDITIONAL(ENABLE_HTTP, [test "$enable_http" = "yes"])
AM_CONDITIONAL(ENABLE_AMQP, [test "$enable_amqp" = "yes"])
AM_CONDITIONAL(ENABLE_STOMP, [test "$enable_stomp" = "yes"])
AM_CONDITIONAL(ENABLE_JSON, [test "$enable_json" = "yes"])
AM_CONDITIONAL(ENABLE_GEOIP2, [test "$enable_geoip2" = "yes"])
AM_CONDITIONAL(ENABLE_REDIS, [test "$enable_redis" = "yes"])
AM_CONDITIONAL(ENABLE_KAFKA, [test "$enable_kafka" = "yes"])
AM_CONDITIONAL(IVYKIS_INTERNAL, [test "x$with_ivykis" = "xinternal"])
AM_CONDITIONAL(JSON_INTERNAL, [test "x$with_jsonc" = "xinternal"])
AM_CONDITIONAL(LIBMONGO_INTERNAL, [test "x$LIBMONGO_SUBDIRS" != "x"])
AM_CONDITIONAL(LIBRABBITMQ_INTERNAL, [test "x$with_librabbitmq_client" = "xinternal"])
AM_CONDITIONAL(ENABLE_RIEMANN, [test "$enable_riemann" != "no"])
AM_CONDITIONAL(ENABLE_JOURNALD, [test "$with_systemd_journal" != "no"])
AM_CONDITIONAL(ENABLE_PYTHON, [test "$enable_python" != "no"])
AM_CONDITIONAL(ENABLE_JAVA, [test "$enable_java" = "yes"])
AM_CONDITIONAL(ENABLE_JAVA_MODULES, [test "$enable_java_modules" = "yes"])
AM_CONDITIONAL(ENABLE_AFSNMP, [test "$enable_afsnmp" = "yes"])
AM_CONDITIONAL(ENABLE_MANPAGES, [test "$enable_manpages" != "no"])
AM_CONDITIONAL(ENABLE_MANPAGES_INSTALL, [test "$enable_manpages_install" != "no"])
AM_CONDITIONAL(ENABLE_NATIVE, [test "$enable_native" != "no"])
AM_CONDITIONAL(ENABLE_EXTRA_WARNINGS, [test "$enable_extra_warnings" = "yes"])
AM_CONDITIONAL(ENABLE_TESTING, [test "$enable_tests" != "no"])
AM_CONDITIONAL(ENABLE_SANITIZER, [test "$with_sanitizer" != "no"])
AM_CONDITIONAL(ENABLE_DEBUG, [test "$enable_debug" != "no"])
AM_CONDITIONAL([HAVE_INOTIFY], [test x$ac_cv_func_inotify_init = xyes])
AM_CONDITIONAL([HAVE_GETRANDOM], [test x$ac_cv_func_getrandom = xyes])
AM_CONDITIONAL([HAVE_FMEMOPEN], [test x$ac_cv_func_fmemopen = xyes])
AM_CONDITIONAL(ENABLE_IPV6, [test $enable_ipv6 = yes])
AM_CONDITIONAL(PYLINT, [test "x$PYLINT" != "x"])


AC_SUBST(PYLINT)
AC_SUBST(timezonedir)
AC_SUBST(pidfiledir)
AC_SUBST(moduledir)
AC_SUBST(python_moduledir)
AC_SUBST(loggenplugindir)
AC_SUBST(toolsdir)
AC_SUBST(config_includedir)
AC_SUBST(scldir)
AC_SUBST(abs_topsrcdir)
AC_SUBST(xsddir)
AC_SUBST(systemdsystemunitdir)
AC_SUBST(SYSLOGNG_LINK)
AC_SUBST(SYSLOGNG_DEPS_LIBS)
AC_SUBST(TOOL_DEPS_LIBS)
AC_SUBST(CORE_DEPS_LIBS)
AC_SUBST(MODULE_DEPS_LIBS)
AC_SUBST(MODULE_LDFLAGS)
AC_SUBST(BASE_LIBS)
AC_SUBST(YFLAGS)
AC_SUBST(LIBNET_LIBS)
AC_SUBST(LIBNET_CFLAGS)
AC_SUBST(NETSNMP_CFLAGS)
AC_SUBST(NETSNMP_LIBS)
AC_SUBST(LIBWRAP_LIBS)
AC_SUBST(LIBWRAP_CFLAGS)
AC_SUBST(ZLIB_LIBS)
AC_SUBST(ZLIB_CFLAGS)
AC_SUBST(LIBDBI_LIBS)
AC_SUBST(LIBDBI_CFLAGS)
AC_SUBST(LIBMONGO_LIBS)
AC_SUBST(LIBMONGO_CFLAGS)
AC_SUBST(LIBMONGO_SUBDIRS)
AC_SUBST(LIBPAHO_MQTT_CFLAGS)
AC_SUBST(LIBPAHO_MQTT_LIBS)
AC_SUBST(LIBESMTP_CFLAGS)
AC_SUBST(LIBESMTP_LIBS)
AC_SUBST(LIBCURL_CFLAGS)
AC_SUBST(LIBCURL_LIBS)
AC_SUBST(LIBRABBITMQ_LIBS)
AC_SUBST(LIBRABBITMQ_CFLAGS)
AC_SUBST(LIBRABBITMQ_SUBDIRS)
AC_SUBST(JSON_LIBS)
AC_SUBST(JSON_CFLAGS)
AC_SUBST(JSON_SUBDIRS)
AC_SUBST(JSON_DEPENDENCY)
AC_SUBST(IVYKIS_SUBDIRS)
AC_SUBST(RESOLV_LIBS)

AC_SUBST(CURRDATE)
AC_SUBST(RELEASE_TAG)
AC_SUBST(SNAPSHOT_VERSION)
AC_SUBST(SOURCE_REVISION)
AC_SUBST(with_ivykis)
AC_SUBST(INTERNAL_IVYKIS_CFLAGS)
AC_SUBST(LIBSYSTEMD_JOURNAL_CFLAGS)
AC_SUBST(LIBSYSTEMD_JOURNAL_LIBS)
AC_SUBST(XSL_STYLESHEET)

AX_PREFIX_CONFIG_H(syslog-ng-config.h, "SYSLOG_NG")
AX_VALGRIND_CHECK

AC_OUTPUT(dist.conf
          Makefile
	  syslog-ng.pc
          libtest/syslog-ng-test.pc
	  scripts/update-patterndb
          )

echo
echo "syslog-ng Open Source Edition $PACKAGE_VERSION configured"
echo " Edition settings:"
echo "  Release type                : $RELEASE_TYPE"
echo "  Pretty version              : $BROCHURE_VERSION"
echo "  Combined vers.              : $COMBINED_VERSION"
echo "  Package name                : $PACKAGE_NAME"
echo " Compiler options:"
echo "  compiler                    : $CC"
echo "  compiler options            : $CFLAGS $CPPFLAGS"
echo "  linker flags                : $LDFLAGS $LIBS"
echo "  prefix                      : $prefix"
echo "  linking mode                : $linking_mode"
echo "  embedded crypto             : ${with_embedded_crypto:=no}"
echo "  __thread keyword            : ${ac_cv_have_tls:=no}"
echo " Test dependencies:"
echo "  Criterion                   : ${with_criterion:=no}"
echo "  Unit tests                  : ${enable_tests:=no}"
echo " Submodules:"
echo "  ivykis                      : $with_ivykis"
echo "  jsonc                       : $with_jsonc"
echo " Features:"
echo "  Forced server mode          : ${enable_forced_server_mode:=yes}"
echo "  Debug symbols               : ${enable_debug:=no}"
echo "  GCC profiling               : ${enable_gprof:=no}"
echo "  Memtrace                    : ${enable_memtrace:=no}"
echo "  IPV6 support                : ${enable_ipv6:=no}"
echo "  spoof-source support        : ${enable_spoof_source:=no}"
echo "  tcp-wrapper support         : ${enable_tcp_wrapper:=no}"
echo "  Linux capability support    : ${has_linux_caps:=no}"
echo "  Env wrapper support         : ${enable_env_wrapper:=no}"
echo "  systemd support             : ${enable_systemd:=no} (unit dir: ${systemdsystemunitdir:=none})"
echo "  systemd-journal support     : ${with_systemd_journal:=no}"
echo " Build options:"
echo "  Generate manual pages       : ${enable_manpages:=no}"
echo "  Install manual pages        : ${enable_manpages_install:=no}"
echo " Modules:"
echo "  Module search path          : ${module_path}"
echo "  Sun STREAMS support (module): ${enable_sun_streams:=no}"
echo "  OpenBSD syslog (module)     : ${enable_openbsd_system_source:=no}"
echo "  SQL support (module)        : ${enable_sql:=no}"
echo "  PACCT module (EXPERIMENTAL) : ${enable_pacct:=no}"
echo "  MongoDB destination (module): ${enable_mongodb:=no}"
echo "  JSON support (module)       : ${enable_json:=no}"
echo "  SMTP support (module)       : ${enable_smtp:=no}"
echo "  HTTP support (module)       : ${enable_http:=no}"
echo "  AMQP destination (module)   : ${enable_amqp:=no}"
echo "  STOMP destination (module)  : ${enable_stomp:=no}"
echo "  MQTT destination (module)   : ${enable_mqtt:=no}"
echo "  GEOIP2 support (module)     : ${enable_geoip2:=no}"
echo "  Redis support (module)      : ${enable_redis:=no}"
echo "  Kafka support (module)      : ${enable_kafka:=no}"
echo "  Riemann destination (module): ${enable_riemann:=no}, microseconds: ${riemann_micros:=no}"
echo "  python                      : ${enable_python:=no} (pkg-config package: ${with_python:=none})"
echo "  java                        : ${enable_java:=no}"
echo "  java modules                : ${enable_java_modules:=no}"
echo "  afsnmp module               : ${enable_afsnmp:=no}"
echo "  native bindings             : ${enable_native:=no}"
