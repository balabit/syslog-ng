Name: syslog-ng
Version: 3.33.1
Release: 2%{?dist}
Summary: Next-generation syslog server

Group: System Environment/Daemons
License: GPLv2+
URL: http://www.balabit.com/network-security/syslog-ng
Source0: https://github.com/syslog-ng/syslog-ng/releases/download/syslog-ng-%{version}/%{name}-%{version}.tar.gz
Source1: syslog-ng.conf
Source2: syslog-ng.logrotate
Source3: syslog-ng.service
# keeping the original logrotate file for RHEL/CentOS 7
# under a new name
Source4: syslog-ng.logrotate7


%bcond_without sql
%bcond_without mongodb
%bcond_without systemd
%bcond_without redis
%bcond_without riemann
%bcond_without maxminddb
%bcond_without amqp
%bcond_without kafka
%bcond_without afsnmp
%bcond_without mqtt


%if 0%{?rhel} == 7
%global		python_devel python36-devel
%global         py_ver  3.6
%else
%global		python_devel python3-devel
%global         py_ver  %{python3_version}
%endif

%if 0%{?rhel} >= 7 || 0%{?fedora} <= 32
%bcond_without java
%else
%bcond_with java
%endif

%global ivykis_ver 0.36.1

BuildRequires: pkgconfig
BuildRequires: libtool
BuildRequires: bison
BuildRequires: flex
BuildRequires: libxslt
BuildRequires: glib2-devel
BuildRequires: ivykis-devel
BuildRequires: json-c-devel
BuildRequires: libcap-devel
BuildRequires: libdbi-devel
BuildRequires: libnet-devel
BuildRequires: openssl-devel
BuildRequires: pcre-devel
BuildRequires: libuuid-devel
BuildRequires: libesmtp-devel
BuildRequires: libcurl-devel

BuildRequires: %{python_devel}

%if %{with amqp}
BuildRequires: librabbitmq-devel
%endif

%if %{with maxminddb}
BuildRequires: libmaxminddb-devel
%endif

%if %{with riemann}
BuildRequires: riemann-c-client-devel
%endif

%if %{with redis}
BuildRequires: hiredis-devel
%endif

%if %{with systemd}
BuildRequires: systemd-units
BuildRequires: systemd-devel
Requires(post): systemd-units
Requires(preun): systemd-units
Requires(postun): systemd-units
%endif

%if %{with mongodb}
BuildRequires: mongo-c-driver-devel
BuildRequires: cyrus-sasl-devel
%endif

%if %{with java}
BuildRequires: java-devel
%endif

%if %{with kafka}
BuildRequires: librdkafka-devel
BuildRequires: zlib-devel
%endif

%if %{with mqtt}
BuildRequires: paho-c-devel
%endif

%if %{with afsnmp}
BuildRequires: net-snmp-devel
%endif

%if 0%{?rhel} == 7
BuildRequires: tcp_wrappers-devel
Obsoletes: syslog-ng-json
%endif

Requires: logrotate
Requires: ivykis >= %{ivykis_ver}

Provides: syslog
# merge separate syslog-vim package into one
Provides: syslog-ng-vim = %{version}-%{release}
Obsoletes: syslog-ng-vim < 2.0.8-1

# Fedora 17’s unified filesystem (/usr-move)
Conflicts: filesystem < 3

%if 0%{?rhel} != 7
Recommends: syslog-ng-logrotate
%endif

%description
syslog-ng is an enhanced log daemon, supporting a wide range of input and
output methods: syslog, unstructured text, message queues, databases (SQL
and NoSQL alike) and more.

Key features:

 * receive and send RFC3164 and RFC5424 style syslog messages
 * work with any kind of unstructured data
 * receive and send JSON formatted messages
 * classify and structure logs with builtin parsers (csv-parser(),
   db-parser(), ...)
 * normalize, crunch and process logs as they flow through the system
 * hand on messages for further processing using message queues (like
   AMQP), files or databases (like PostgreSQL or MongoDB).

%package sql
Summary: SQL support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description sql
This module supports a large number of SQL database systems via libdbi.

%package amqp
Summary: AMQP support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description amqp
This module supports AMQP via librabbitmq

%package mongodb
Summary: mongodb support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description mongodb
This module supports the mongodb database via libmongo-client.

%package smtp
Summary: smtp support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description smtp
This module supports sending e-mail alerts through an smtp server.

%package kafka
Summary: kafka support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description kafka
This module supports sending logs to kafka through librdkafka.

%package afsnmp
Summary: SNMP support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description afsnmp
This module supports sending SNMP traps using net-snmp.

%package mqtt
Summary: mqtt support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description mqtt
This module supports sending logs to mqtt through MQTT.

%package java
Summary:        Java destination support for syslog-ng
Group:          System/Libraries
Requires:       %{name} = %{version}

%description java
This package provides java destination support for syslog-ng. It
only contains the java bindings, no drivers.


%package geoip
Summary: geoip support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description geoip
This package provides GeoIP support for syslog-ng


%package redis
Summary: redis support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description redis
This module supports the redis key-value store via hiredis.

%package riemann
Summary: riemann support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description riemann
This module supports the riemann monitoring server.

%package http
Summary: HTTP support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description http
This module supports the HTTP destination.

%package slog
Summary: $(slog) support for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description slog
This module adds support for the $(slog) template function plus command line utilities.

%package python
Summary:        Python destination support for syslog-ng
Group:          System/Libraries
Requires:       %{name} = %{version}

%description python
This package provides python destination support for syslog-ng.

%package devel
Summary: Development files for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%package logrotate
Summary: Logrotate script for %{name}
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
Conflicts: rsyslog

%description logrotate
This package provides a logrotate script for syslog-ng. It is only installed if
ryslog is not on the system.

%prep
%setup -q
# %patch0 -p1
# %patch1 -p1
# %patch2 -p1

# fix perl path
%{__sed} -i 's|^#!/usr/local/bin/perl|#!%{__perl}|' contrib/relogger.pl

# fix executable perms on contrib files
%{__chmod} -c a-x contrib/syslog2ng

%build

%configure \
    --prefix=%{_prefix} \
    --sysconfdir=%{_sysconfdir}/%{name} \
    --localstatedir=%{_sharedstatedir}/%{name} \
    --datadir=%{_datadir} \
    --with-module-dir=%{_libdir}/%{name} \
    --with-systemdsystemunitdir=%{_unitdir} \
    --with-ivykis=system \
%if 0%{?rhel} == 7
    --enable-tcp-wrapper \
%else
    --disable-tcp-wrapper \
%endif
    --with-embedded-crypto \
    --enable-manpages \
    --enable-ipv6 \
    --enable-spoof-source \
    --with-linux-caps=auto \
    --enable-json \
    --enable-ssl \
    --enable-smtp \
    --enable-shared \
    --disable-static \
    --enable-dynamic-linking \
    --enable-python \
    --disable-java-modules \
    --with-python=%{py_ver} \
    %{?with_kafka:--enable-kafka} \
    %{?with_mqtt:--enable-mqtt} \
    %{?with_afsnmp:--enable-afsnmp} %{!?with_afsnmp:--disable-afsnmp} \
    %{?with_java:--enable-java} %{!?with_java:--disable-java} \
    %{?with_maxminddb:--enable-geoip2} %{!?with_maxminddb:--disable-geoip2} \
    %{?with_sql:--enable-sql} \
    %{?with_systemd:--enable-systemd} \
    %{?with_mongodb:--enable-mongodb} \
    %{?with_amqp:--enable-amqp} \
    %{?with_redis:--enable-redis} \
    %{?with_riemann:--enable-riemann}

# disable broken test by setting a different target
sed -i 's/libs build/libs assemble/' Makefile

make %{_smp_mflags}


%install
make DESTDIR=%{buildroot} install

%{__install} -d -m 755 %{buildroot}%{_sysconfdir}/%{name}/conf.d
%{__install} -p -m 644 %{SOURCE1} %{buildroot}%{_sysconfdir}/%{name}/syslog-ng.conf

%{__install} -d -m 755 %{buildroot}%{_sysconfdir}/logrotate.d
%if 0%{?rhel} == 7
%{__install} -p -m 644 %{SOURCE4} %{buildroot}%{_sysconfdir}/logrotate.d/syslog
%endif
%if 0%{?rhel} == 8
%{__install} -p -m 644 %{SOURCE2} %{buildroot}%{_sysconfdir}/logrotate.d/syslog
%endif
%if 0%{?fedora} >= 28
%{__install} -p -m 644 %{SOURCE2} %{buildroot}%{_sysconfdir}/logrotate.d/syslog-ng
%endif

%if %{with systemd}
%{__install} -p -m 644 %{SOURCE3} %{buildroot}%{_unitdir}/%{name}.service
# remove unused service file
rm %{buildroot}/usr/lib/systemd/system/syslog-ng@.service
%endif


# create the local state dir
%{__install} -d -m 755 %{buildroot}/%{_sharedstatedir}/%{name}

# install the main library header files
%{__install} -d -m 755 %{buildroot}%{_includedir}/%{name}
%{__install} -p -m 644 config.h %{buildroot}%{_includedir}/%{name}
%{__install} -p -m 644 lib/*.h %{buildroot}%{_includedir}/%{name}

# install vim files
%{__install} -d -m 755 %{buildroot}%{_datadir}/%{name}
%{__install} -p -m 644 contrib/syslog-ng.vim %{buildroot}%{_datadir}/%{name}
for vimver in 73 ; do
    %{__install} -d -m 755 %{buildroot}%{_datadir}/vim/vim$vimver/syntax
    cd %{buildroot}%{_datadir}/vim/vim$vimver/syntax
    ln -s ../../../%{name}/syslog-ng.vim .
    cd -
done

find %{buildroot} -name "*.la" -exec rm -f {} \;


%post
ldconfig
%systemd_post syslog-ng.service

%preun
%systemd_preun syslog-ng.service

%postun
ldconfig
%systemd_postun_with_restart syslog-ng.service


%triggerun -- syslog-ng < 3.2.3
if /sbin/chkconfig --level 3 %{name} ; then
    /bin/systemctl enable %{name}.service >/dev/null 2>&1 || :
fi


%triggerin -- vim-common
VIMVERNEW=`rpm -q --qf='%%{epoch}:%%{version}\n' vim-common | sort | tail -n 1 | sed -e 's/[0-9]*://' | sed -e 's/\.[0-9]*$//' | sed -e 's/\.//'`
[ -d %{_datadir}/vim/vim${VIMVERNEW}/syntax ] && \
    cd %{_datadir}/vim/vim${VIMVERNEW}/syntax && \
    ln -sf ../../../%{name}/syslog-ng.vim . || :

%triggerun -- vim-common
VIMVEROLD=`rpm -q --qf='%%{epoch}:%%{version}\n' vim-common | sort | head -n 1 | sed -e 's/[0-9]*://' | sed -e 's/\.[0-9]*$//' | sed -e 's/\.//'`
[ $2 = 0 ] && rm -f %{_datadir}/vim/vim${VIMVEROLD}/syntax/syslog-ng.vim || :

%triggerpostun -- vim-common
VIMVEROLD=`rpm -q --qf='%%{epoch}:%%{version}\n' vim-common | sort | head -n 1 | sed -e 's/[0-9]*://' | sed -e 's/\.[0-9]*$//' | sed -e 's/\.//'`
VIMVERNEW=`rpm -q --qf='%%{epoch}:%%{version}\n' vim-common | sort | tail -n 1 | sed -e 's/[0-9]*://' | sed -e 's/\.[0-9]*$//' | sed -e 's/\.//'`
if [ $1 = 1 ]; then
    rm -f %{_datadir}/vim/vim${VIMVEROLD}/syntax/syslog-ng.vim || :
    [ -d %{_datadir}/vim/vim${VIMVERNEW}/syntax ] && \
        cd %{_datadir}/vim/vim${VIMVERNEW}/syntax && \
        ln -sf ../../../%{name}/syslog-ng.vim . || :
fi


%files
%doc AUTHORS COPYING NEWS.md
# %doc doc/security/*.txt
%doc contrib/{relogger.pl,syslog2ng,syslog-ng.conf.doc}

%dir %{_sysconfdir}/%{name}
%dir %{_sysconfdir}/%{name}/conf.d
%dir %{_sysconfdir}/%{name}/patterndb.d
%config(noreplace) %{_sysconfdir}/%{name}/%{name}.conf
%config(noreplace) %{_sysconfdir}/%{name}/scl.conf
%if 0%{?rhel} == 7
%config(noreplace) %{_sysconfdir}/logrotate.d/syslog
%endif
%dir %{_sharedstatedir}/%{name}
%{_sbindir}/%{name}
%{_sbindir}/%{name}-debun
%{_sbindir}/syslog-ng-ctl
%{_bindir}/loggen
%{_bindir}/pdbtool
%{_bindir}/dqtool
%{_bindir}/update-patterndb
%{_bindir}/persist-tool
%{_libdir}/lib%{name}-*.so.*
%{_libdir}/libevtlog-*.so.*
%{_libdir}/libsecret-storage.so.*
%{_libdir}/libloggen_helper-*.so.*
%{_libdir}/libloggen_plugin-*.so.*
%{_libdir}/%{name}/libadd-contextual-data.so
%{_libdir}/%{name}/libaffile.so
%{_libdir}/%{name}/libafprog.so
%{_libdir}/%{name}/libafsocket.so
%{_libdir}/%{name}/libafstomp.so
%{_libdir}/%{name}/libafuser.so
%{_libdir}/%{name}/libappmodel.so
%{_libdir}/%{name}/libbasicfuncs.so
%{_libdir}/%{name}/libcef.so
%{_libdir}/%{name}/libconfgen.so
%{_libdir}/%{name}/libcryptofuncs.so
%{_libdir}/%{name}/libcsvparser.so
%{_libdir}/%{name}/libtimestamp.so
%{_libdir}/%{name}/libdbparser.so
%{_libdir}/%{name}/libdisk-buffer.so
%{_libdir}/%{name}/libexamples.so
%{_libdir}/%{name}/libgraphite.so
%{_libdir}/%{name}/libhook-commands.so
%{_libdir}/%{name}/libjson-plugin.so
%{_libdir}/%{name}/libkvformat.so
%{_libdir}/%{name}/liblinux-kmsg-format.so
%{_libdir}/%{name}/libmap-value-pairs.so
%{_libdir}/%{name}/libpseudofile.so
%{_libdir}/%{name}/libregexp-parser.so
%{_libdir}/%{name}/libstardate.so
%{_libdir}/%{name}/libsyslogformat.so
%{_libdir}/%{name}/libsystem-source.so
%{_libdir}/%{name}/libtags-parser.so
%{_libdir}/%{name}/libtfgetent.so
%{_libdir}/%{name}/libxml.so

%if %{with systemd}
%{_unitdir}/%{name}.service
%{_libdir}/%{name}/libsdjournal.so
%endif

%dir %{_libdir}/%{name}/loggen
%{_libdir}/%{name}/loggen/libloggen*

%dir %{_datadir}/%{name}
%{_datadir}/%{name}/syslog-ng.vim
%ghost %{_datadir}/vim/

# scl files
%{_datadir}/%{name}/include/

# uhm, some better places for those?
%{_datadir}/%{name}/xsd/

%{_mandir}/man1/loggen.1*
%{_mandir}/man1/pdbtool.1*
%{_mandir}/man1/dqtool.1*
%{_mandir}/man1/persist-tool.1*
%{_mandir}/man1/syslog-ng-debun.1*
%{_mandir}/man1/syslog-ng-ctl.1*
%{_mandir}/man5/syslog-ng.conf.5*
%{_mandir}/man8/syslog-ng.8*

%if %{with sql}
%files sql
%{_libdir}/%{name}/libafsql.so
%endif

%if %{with amqp}
%files amqp
%{_libdir}/%{name}/libafamqp.so
%endif

%if %{with mongodb}
%files mongodb
%{_libdir}/%{name}/libafmongodb.so
%endif

%if %{with redis}
%files redis
%{_libdir}/%{name}/libredis.so
%endif

%if %{with kafka}
%files kafka
%{_libdir}/%{name}/libkafka.so
%endif

%if %{with afsnmp}
%files afsnmp
%{_libdir}/%{name}/libafsnmp.so
%endif

%if %{with mqtt}
%files mqtt
%{_libdir}/%{name}/libmqtt.so
%endif

%files smtp
%{_libdir}/%{name}/libafsmtp.so

%if %{with java}
%files java
%attr(755,root,root) %{_libdir}/syslog-ng/libmod-java.so
%dir %{_libdir}/%{name}/java-modules/
%{_libdir}/%{name}/java-modules/*
%endif

%if %{with maxminddb}
%files geoip
%{_libdir}/%{name}/libgeoip2-plugin.so
%endif

%if %{with riemann}
%files riemann
%{_libdir}/%{name}/libriemann.so
%endif

%files http
%{_libdir}/%{name}/libhttp.so
%{_libdir}/%{name}/libazure-auth-header.so

%files slog
%{_libdir}/%{name}/libsecure-logging.so
%{_bindir}/slogkey
%{_bindir}/slogencrypt
%{_bindir}/slogverify
%{_mandir}/man1/slogkey.1*
%{_mandir}/man1/slogencrypt.1*
%{_mandir}/man1/slogverify.1*
%{_mandir}/man7/secure-logging.7*

%files python
%{_libdir}/%{name}/python/syslogng-1.0-py%{py_ver}.egg-info
%{_libdir}/%{name}/python/syslogng/*
%{_libdir}/%{name}/libmod-python.so

%files devel
%{_libdir}/libsyslog-ng.so
%{_libdir}/libevtlog.so
%{_libdir}/libsecret-storage.so
%{_libdir}/libsyslog-ng-native-connector.a
%{_libdir}/libloggen_helper.so
%{_libdir}/libloggen_plugin.so

%if 0%{?_dbld}

# without criterion we don't have the test lib.  On dbld we do have it, on
# upstream CentOS/Fedora we don't.

%{_libdir}/%{name}/libtest/libsyslog-ng-test.a
%{_libdir}/pkgconfig/syslog-ng-test.pc
%endif

%{_includedir}/%{name}/
%{_libdir}/pkgconfig/syslog-ng.pc
%{_libdir}/pkgconfig/syslog-ng-native-connector.pc
%{_datadir}/%{name}/tools/

%if 0%{?rhel} != 7
%files logrotate
%if 0%{?rhel} == 8
%config(noreplace) %{_sysconfdir}/logrotate.d/syslog
%else
%config(noreplace) %{_sysconfdir}/logrotate.d/syslog-ng
%endif
%endif


%changelog
* Mon Jul  5 2021 github-actions <github-actions@github.com> - 3.33.1-1
- updated to 3.33.1

* Fri May  7 2021 github-actions <github-actions@github.com> - 3.32.1-1
- updated to 3.32.1

* Wed Mar 17 2021 github-actions <github-actions@github.com> - 3.31.2-1
- updated to 3.31.2

* Tue Mar  2 2021 github-actions <github-actions@github.com> - 3.31.1-1
- updated to 3.31.1

* Wed Feb  3 2021 Peter Czanik <peter@czanik.hu> - 3.30.1-2
- update to 3.30.1
- remove Python 2 support
- remove CentOS 6 support

* Mon Aug 17 2020 Peter Czanik <peter@czanik.hu> - 3.28.1-2
- keep obsolate Obsolates about syslog-ng-json only for EL7

* Mon Jun 22 2020 Peter Czanik <peter@czanik.hu> - 3.28.1-1
- update to 3.28.1
- disable compiling java modules

* Tue Jun 16 2020 Peter Czanik <peter@czanik.hu> - 3.27.1-1
- raneme logrotate file from syslog to syslog-ng on Fedora, move it
  to subpackage on Fedora & RHEL 8 to fix: rhbz#1802165

* Mon Mar  2 2020 Laszlo Budai <laszlo.budai@outlook.com> - 3.26.1-1
- update to 3.26.1

* Tue Dec 10 2019 Attila Szakacs <attila.szakacs@balabit.com> - 3.25.1-1
- update to 3.25.1

* Tue Oct 01 2019 Laszlo Budai <laszlo.budai@balabit.com> - 3.24.1-1
- update to 3.24.1

* Fri Aug 16 2019 Laszlo Budai <laszlo.budai@balabit.com> - 3.23.1-1
- update to 3.23.1

* Sun Jun 16 2019 Balazs Scheidler <balazs.scheidler@oneidentity.com> - 3.22.1-1
- merge changes from Peter Czanik's copr specfile
- make this work on all supported distros (centos-6, centos-7, latest fedora releases)

* Fri May  3 2019 Laszlo Budai <laszlo.budai@balabit.com> - 3.21.1-1
- update to 3.21.1

* Tue Feb 26 2019 Gabor Nagy <gabor.nagy@balabit.com> - 3.20.1-1
- update to 3.20.1

* Tue Dec  4 2018 Laszlo Varady <laszlo.varady@balabit.com> - 3.19.1-1
- update to 3.19.1

* Thu Oct 11 2018 Laszlo Budai <laszlo.budai@balabit.com> - 3.18.1-1
- update to 3.18.1

* Fri Aug 10 2018 Laszlo Budai <laszlo.budai@balabit.com> - 3.17.2-1
- update to 3.17.2

* Thu Aug  9 2018 Laszlo Budai <laszlo.budai@balabit.com> - 3.17.1-1
- update to 3.17.1

* Thu Jun 14 2018 Gabor Nagy <gabor.nagy@balabit.com> - 3.16.1-1
- update to 3.16.1

* Wed May 30 2018 Peter Czanik <peter@czanik.hu> - 3.15.1-2
- add support for modularized loggen
- use external rabbitmq-devel as it's no more bundled

* Tue May  8 2018 Laszlo Szemere <laszlo.szemere@balabit.com> - 3.15.1-1
- update to 3.15.1

* Thu Mar 29 2018 Peter Czanik <peter@czanik.hu> - 3.14.1-7
- remove syslog-ng-add-contextual-data.pc from file list

* Tue Feb 27 2018 Peter Czanik <peter@czanik.hu> - 3.14.1-1
- update to 3.14.1
- re-enable amqp & mongodb on RHEL only
- keep tcp wrappers support only on RHEL

* Mon Dec  4 2017 Peter Czanik <peter@czanik.hu> - 3.13.1-2
- update to 3.13.1
- disable AMQP temporarily due to openssl 1.1 problems
- disable MongoDB temporarily due to compile problems

* Thu Sep 21 2017 Peter Czanik <peter@czanik.hu> - 3.12.1-3
- update to 3.12.1
- re-enabled AMQP (does not fix compilation on Fedora)
- update logrotate to match rsyslog

* Wed Sep 13 2017 Peter Czanik <peter@czanik.hu> - 3.11.1-5
- update to latest git head
- disable AMQP (rabbitmq) support temporarily

* Wed Jul 26 2017 Peter Czanik <peter@czanik.hu> - 3.11.1-2
- update to 3.11.1
- commented out "remove rpath" fixes for now

* Mon Jun 19 2017 Peter Czanik <peter@czanik.hu> - 3.10.1-1
- update to 3.10.1

* Tue Jan 17 2017 Peter Czanik <czanik@balabit.hu> - 3.9.1-2
- update to git head
- rename libcurl.so to libhttp.so
- add libtfgetent.so

* Fri Dec 23 2016 Peter Czanik <czanik@balabit.hu> - 3.9.1-1
- update to 3.9.1

* Fri Dec  2 2016 Peter Czanik <czanik@balabit.hu> - 3.8.1-3
- update to git head
- add pkgconfig file for add-contextual-data

* Tue Sep  6 2016 Peter Czanik <czanik@balabit.hu> - 3.8.1-2
- move curl (HTTP) support into a subpackage
  to reduce number of dependencies in core package
- re-enable riemann support
- use local cache of JAR dependencies
  (no Internet necessary)

* Thu Aug 25 2016 Peter Czanik <czanik@balabit.hu> - 3.8.1-1
- update to 3.8.1
- enable curl (HTTP) support
- JAR filelist changes

* Thu Jul 28 2016 Peter Czanik <czanik@balabit.hu> - 3.8.0beta1-1
- update to 3.8 beta1

* Fri Mar 18 2016 Peter Czanik <czanik@balabit.hu> - 3.8.0alpha0-1
- disable riemann to ease test builds
- update to 3.8 git head
- remove syslog-ng-3.4.0beta1-tests-functional-control.py.patch
- remove syslog-ng-3.3.6-tests-functional-sql-test.patch
- bump config version
- disable "make check" (pylint fails)

* Thu Nov  5 2015 Peter Czanik <czanik@balabit.hu> - 3.7.2-2
- added missing "Obsoletes: syslog-ng-json" as JSON support
  is now moved to the core

* Wed Oct 28 2015 Peter Czanik <peter.czanik@balabit.com> - 3.7.2-1
- update to 3.7.2

* Tue Aug 18 2015 Peter Czanik <peter.czanik@balabit.com> - 3.7.1-2
- update to 3.7.1
- require syslog-ng-java-hack for building
- hack Makefile to disable failing test
- add path to gradle-2.6

* Thu Jul 23 2015 Peter Czanik <peter.czanik@balabit.com> - 3.7.0beta2-1
- update to 3.7.0beta2
- re-enable man pages
- require gradle (for Java modules)
- update syslog-ng.service patch

* Tue Apr 21 2015 Peter Czanik <peter.czanik@balabit.com> - 3.7.0beta1-5
- update to 3.7.0beta1
- temp: disable man pages (missing from release)
- temp: disable smp build
- add python support
- add java support
- move json support to the core package due to system()
- update configuration version to 3.7
- add BuildRequires to fix "make check"

* Tue Dec 16 2014 Peter Czanik <czanik@balabit.hu> - 3.6.2-1
- update to syslog-ng 3.6.2 (bugfix release)
- disabled "make check" temporarily due to a false positive

* Fri Nov 14 2014 Peter Czanik <czanik@balabit.hu> - 3.6.1-1
- update to syslog-ng 3.6.1
- enable riemann-c-client support

* Fri Sep 26 2014 Peter Czanik <czanik@balabit.hu> - 3.6.0rc1-1
- update to syslog-ng 3.6.0rc1
- removed --enable-pcre, as it's always required
- configuration file version bump to 3.6

* Thu Aug 21 2014 Kevin Fenzi <kevin@scrye.com> - 3.5.6-3
- Rebuild for rpm bug 1131960

* Mon Aug 18 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.5.6-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_22_Mass_Rebuild

* Tue Aug  5 2014 Peter Czanik <czanik@balabit.hu> - 3.5.6-1
- Update to syslog-ng 3.5.6 (bugfix release)

* Wed Jul 23 2014 Peter Czanik <czanik@balabit.hu> - 3.5.5-1
- Update to syslog-ng 3.5.5 (bugfix release)

* Sun Jun 08 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.5.4.1-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_Mass_Rebuild

* Fri May 23 2014 Peter Czanik <czanik@balabit.hu> - 3.5.4.1-2
- enable SCL in syslog-ng.conf
- use system() in syslog-ng.conf
- move JSON, SMTP and GeoIP support to separate subpackages
  due to dependencies

* Tue Mar 18 2014 Jose Pedro Oliveira <jose.p.oliveira.oss at gmail.com> - 3.5.4.1-1
- Update to syslog-ng 3.5.4.1

* Sat Feb 22 2014 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.5.3-4
- Upstream patch:  add support for the Tzif3 timezone files
  (syslog-ng-3.5.3-support-Tzif3-format-timezone-files.patch)

* Tue Feb 11 2014 Matthias Runge <mrunge@redhat.com> - 3.5.3-3
- rebuild due libdbi bump

* Wed Jan 22 2014 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.5.3-2
- Bump configuration file version to 3.5
- Rebuild for libdbi soname bump

* Wed Dec 25 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.5.3-1
- Update to syslog-ng 3.5.3

* Fri Nov 29 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.5.2-1
- Update to syslog-ng 3.5.2

* Thu Nov 21 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.5.1-2
- New upstream package description (Balabit; Peter Czanik)

* Mon Nov  4 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.5.1-1
- Update to syslog-ng 3.5.1 (first stable release of branch 3.5)
- New build requirement: libxslt (--enable-man-pages)

* Thu Oct 24 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.5.0-0.rc1.1
- Update to syslog-ng 3.5.0 rc 1
- Re-enabled parallel build

* Sat Oct 19 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.5.0-0.beta3.1
- Update to syslog-ng 3.5.0 beta 3
- Updated source0 URL
- syslog-ng.service patch rebased (syslog-ng-3.5.0-syslog-ng.service.patch)
- New BR: systemd-devel
- New subpackage: syslog-ng-redis (new BR: hiredis-devel)
- Disabled parallel build (currently fails)

* Thu Oct 17 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.4.4-1
- Update to syslog-ng 3.4.4

* Tue Aug 13 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.4.3-1
- Update to syslog-ng 3.4.3

* Sun Aug 04 2013 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.4.1-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_20_Mass_Rebuild

* Thu Jan 31 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.4.1-1
- Update to syslog-ng 3.4.1 (first stable version of branch 3.4)

* Sat Jan 19 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.4.0-0.1.rc2
- Update to syslog-ng 3.4.0 rc2
- Bumped the eventlog version requirement to 0.2.13
- Bumped the ivykis version requirement to 0.36.1
- New build requirement: GeoIP-devel (--enable-geoip)
- New build requirement: libuuid-devel
- New build requirement: libesmtp-devel (--enable-smtp)
- New build requirement: libmongo-client--devel (--with-libmongo-client=system)
- Splitted the mongodb support into a subpackage
- Rebased the syslog-ng-3.2.5-tests-functional-control.py.patch patch
- Disable the AMQP support (until it builds with an external librabbitmq library)

* Sat Jan 19 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.3.8-2
- Corrected bogus dates in the changelog section

* Thu Jan 17 2013 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.3.8-1
- Update to 3.3.8
- Use the new --with-embedded-crypto configure's option in order to
  avoid shipping a ld.so.conf file

* Fri Nov 30 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.3.7-3
- Introduce the new systemd-rpm macros (#850332)

* Fri Nov 30 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.3.7-2
- Rename ivykis-ver to ivykis_ver (invalid character)

* Tue Oct 30 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.3.7-1
- Update to 3.3.7

* Thu Oct 18 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.3.7-0.1.rc2
- Update to 3.3.7 RC2 (aka syslog-ng-3.3.6.91-20121008-v3.3.6.91.tar.gz)
- Create and own the /etc/syslog-ng/conf.d directory
- syslog-ng.conf: now sources additional configuration files located at
  /etc/syslog-ng/conf.d ; these files must have a .conf extension
- syslog-ng.conf: make the s_sys source more compliant with the one
  generated by generate-system-source.sh
- syslog-ng.conf: retab
- Bump the minimal ivykis version requirement to 0.30.4

* Mon Aug 27 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.3.6-1
- Update to 3.3.6
- Now builds with an external (and unpatched) version of the ivykis library (>= 0.30.1)
- Enable JSON support (BR json-c-devel).
- Enable Linux caps (BR libcap-devel).
- BR bison and flex
- syslog-ng.conf: rename the now obsolete long_hostnames option to chain_hostnames
- install a ld.so conf file so that the private shared library -
  libsyslog-ng-crypto - can be found.
- Unconditionally run "systemctl daemon-reload" on the %%postun scriptlet
  (https://bugzilla.redhat.com/show_bug.cgi?id=700766#c25)

* Sat Jul 21 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.2.5-16
- Rebuilt for https://fedoraproject.org/wiki/Fedora_18_Mass_Rebuild

* Tue Jun 19 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-15
- Remove the ExecStartPre line from the service file (#833551)

* Thu Apr 26 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-14
- Improve syslog-ng-3.2.5-tests-functional-sql-test.patch

* Thu Apr 26 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-13
- Add a conflict with the filesystem package (due to the /usr-move)

* Mon Apr 16 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-12
- No longer disable the SSL tests.

* Mon Apr 16 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-11
- Correct the path in syslog-ng-3.2.5-syslog-ng.service.patch.

* Mon Apr 16 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-10
- Enable SSL.

* Sun Apr 15 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-9
- Fedora 17’s unified filesystem (/usr-move)
  http://fedoraproject.org/wiki/Features/UsrMove

* Sun Apr 15 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-8
- Resolve the file conflict with rsyslog (#811058).
- Don't tag the syslog-ng.service file as a configuration file.

* Fri Mar 30 2012 Jon Ciesla <limburgher@gmail.com> - 3.2.5-7
- Rebuild for updated libnet.

* Fri Feb 10 2012 Petr Pisar <ppisar@redhat.com> - 3.2.5-6
- Rebuild against PCRE 8.30

* Sun Jan 15 2012 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-5
- Improve test coverage: remove a couple of errors and really run the SQL test.
  Patches: syslog-ng-3.2.5-tests-functional-control.py.patch and
  syslog-ng-3.2.5-tests-functional-sql-test.patch.

* Sat Jan 14 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.2.5-4
- Rebuilt for https://fedoraproject.org/wiki/Fedora_17_Mass_Rebuild

* Thu Dec 15 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-3
- Drop the sysconfig configuration file (use syslog-ng.service instead)
- Make the syslog-ng.service file a configuration file
- Drop Vim 7.2 support

* Wed Dec 14 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-2
- Fix the freeze problems caused by the /dev/log unix socket type mismatch (#742624)
  + syslog-ng.conf: change /dev/log from unix-stream to unix-dgram
  + upstream patch syslog-ng-3.3.4-afunix.c-diagnostic-messages.patch
- Move the SCL files to the main RPM (#742624 comments >= 28)

* Tue Nov  1 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.5-1
- Update to 3.2.5

* Tue Oct 25 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-11
- New 3.2.5 pre-release tarball
  https://lists.balabit.hu/pipermail/syslog-ng/2011-October/017491.html
- Updated patch syslog-ng-3.2.5-syslog-ng.service.patch

* Sat Oct 22 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-10
- 3.2.5 pre-release: changelog and tarball from
  https://lists.balabit.hu/pipermail/syslog-ng/2011-October/017462.html
  Patches dropped:
  syslog-ng-3.2.4-systemd-acquired-fd.patch
  syslog-ng-3.2.4-chain-hostnames-processing.patch
- New configure option: --with-systemdsystemunitdir
- Patched the included syslog-ng.service file
  syslog-ng-3.2.5-syslog-ng.service.patch

* Mon Oct 10 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-9
- Patch syslog-ng-3.2.4-systemd-acquired-fd.patch (see bug #742624)

* Mon Oct 10 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-8
- disable linux-caps support for the time being (see bug #718439)

* Wed Aug 31 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-7
- Fixed the syslog-ng.service configuration file:
  * Sockets setting (#734569)
  * StandardOutput setting (#734591)

* Mon Jun 27 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-6
- Patch syslog-ng-3.2.4-chain-hostnames-processing.patch (#713965)

* Mon Jun 20 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-5
- Enabled support for capability management (--enable-linux-caps)

* Tue May 17 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-4
- Enabled SQL support (subpackage syslog-ng-libdbi)

* Mon May 16 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-3
- Updated the homepage URL
- Syslog-ng data directory in %%{_datadir}/%%{name}
- Include the main library header files in the devel subpackage

* Thu May 12 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-2
- No need to create the directory /etc/syslog-ng in the install section
- Enable the test suite (but excluding the SQL and SSL tests)

* Wed May 11 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.4-1
- Update to 3.2.4

* Mon May  9 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.3-5
- Overrided the default _localstatedir value (configure --localstatedir)
  (value hardcoded in update-patterndb)
- Manually created the patterndb.d configuration directory (update-patterndb)
  (see also https://bugzilla.balabit.com/show_bug.cgi?id=119 comments >= 4)
- Dropped support for Vim 7.0 and 7.1

* Mon May  9 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.3-4
- Dropped the bison and flex build requirements
- Corrected a couple of macro references in changelog entries (rpmlint)

* Mon May  9 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.3-3
- Added the build requirement systemd-units (macro %%_unitdir)
  https://fedoraproject.org/wiki/Packaging:Guidelines:Systemd
- Dropped the redefinition of the %%_localstatedir macro
- Use %%global instead of %%define
- Minor modifications of the %%post, %%preun and %%postun scripts
  https://fedoraproject.org/wiki/Packaging:ScriptletSnippets#Systemd
- Expanded tabs to spaces (also added a vim modeline)

* Fri May  6 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.3-2
- Fix systemd-related scriptlets (Bill Nottingham)
- Explicitly add --enable-systemd to configure's command line

* Mon May  2 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.3-1
- updated to 3.2.3 final
- cleaned the sysconfig file

* Thu Apr 28 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.3-0.20110424.4
- downgrade the pcre minimal required version from 7.3 to 6.1 (#651823#c26)
- better compliance with the package guidelines
  (https://fedoraproject.org/wiki/Packaging:ScriptletSnippets#Systemd)

* Thu Apr 28 2011 Matthias Runge <mrunge@matthias-runge.de> - 3.2.3-0.20110424.3
- honor pidfile
- disable ssl
- disable sql

* Tue Apr 26 2011 Matthias Runge <mrunge@matthias-runge.de> - 3.2.3-0.20110424.2
- drop support for fedora without systemd

* Mon Apr 25 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.3-0.20110424.1
- change NVR to alert users that we have been using a syslog-ng v3.2 git snapshot
  (for systemd support)

* Mon Apr 25 2011 Jose Pedro Oliveira <jpo at di.uminho.pt> - 3.2.2-4
- re-introduces the "Provides: syslog" (#651823 comments 13, 15 and 21)
- rename the logrotate.d file back to syslog (#651823 comments 12, 15, 16 and 21)
- cleans the sysconfig and logrotate file mess (#651823 comments 17, 20 and 21)
- spec code cleanup (#651823 comments 10 and 11)
- dropped duplicated eventlog-devel BR

* Thu Apr 21 2011 Matthias Runge <mrunge@matthias-runge.de> - 3.2.2-3
- systemd fixup
- more spec file cleanup,
- incorporate fixes from Jose Pedro Oliveira (#651823 comments 7 and 8)

* Wed Apr 20 2011 Matthias Runge <mrunge@matthias-runge.de> - 3.2.2-2
- spec cleanup

* Wed Apr 13 2011 Matthias Runge <mrunge@matthias-runge.de> - 3.2.2-1
- update to 3.2.2
- built from git snapshot

* Wed Apr 06 2011 Matthias Runge <mrunge@matthias-runge.de> - 3.2.1-3
- install to /sbin
- native systemd start script

* Thu Mar 17 2011 Matthias Runge <mrunge@matthias-runge.de> - 3.2.1-2
- finally move libs to correct place
- split out -devel subpackage

* Fri Mar 04 2011 Matthias Runge <mrunge@fedoraproject.org> - 3.2.1-1
- update to syslog-ng 3.2.1

* Sat Jul 24 2010 Doug Warner <silfreed@fedoraproject.org> - 3.1.1-1
- update for syslog-ng 3.1.1
- supports the new syslog protocol standards
- log statements can be embedded into each other
- the encoding of source files can be set for proper character conversion
- can read, process, and rewrite structured messages (e.g., Apache webserver
  logs) using templates and regular expressions
- support for patterndb v2 and v3 format, along with a bunch of new
  parsers: ANYSTRING, IPv6, IPvANY and FLOAT.
- added a new "pdbtool" utility to manage patterndb files: convert them
  from v1 or v2 format, merge mulitple patterndb files into one and look
  up matching patterns given a specific message.
- support for message tags: tags can be assigned to log messages as they
  enter syslog-ng: either by the source driver or via patterndb.
  Later it these tags can be used for efficient filtering.
- added support for rewriting structured data
- added pcre support in the binary packages of syslog-ng

* Tue Sep 15 2009 Ray Van Dolson <rayvd@fedoraproject.org> - 2.1.4-8
- Adjust eventlog build requirement

* Tue Sep 15 2009 Ray Van Dolson <rayvd@fedoraproject.org> - 2.1.4-7
- Branch sync

* Tue Sep 15 2009 Ray Van Dolson <rayvd@fedoraproject.org> - 2.1.4-6
- Branch sync

* Tue Sep 15 2009 Ray Van Dolson <rayvd@fedoraproject.org> - 2.1.4-5
- Rebuilding for tag issue

* Thu Aug 20 2009 Ray Van Dolson <rayvd@fedoraproject.org> - 2.1.4-4
- libnet linking (bug#518150)

* Tue Aug 18 2009 Ray Van Dolson <rayvd@fedoraproject.org> - 2.1.4-3
- Init script fix (bug#517339)

* Sun Jul 26 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.1.4-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_12_Mass_Rebuild

* Tue Mar 24 2009 Douglas E. Warner <silfreed@silfreed.net> - 2.1.4-1
- update to 2.1.4
- enabling mixed linking to compile only non-system libs statically
- lots of packaging updates to be able to build on RHEL4,5, Fedora9+ and be
  parallel-installable with rsyslog and/or sysklogd on those platforms
- removing BR for flex & byacc to try to prevent files from being regenerated
- fixing build error with cfg-lex.l and flex 2.5.4
- Fixed a possible DoS condition triggered by a destination port unreachable
  ICMP packet received from a UDP destination.  syslog-ng started eating all
  available memory and CPU until it crashed if this happened.
- Fixed the rate at which files regular were read using the file() source.
- Report connection breaks as a write error instead of reporting POLLERR as
  the write error path reports more sensible information in the logs.

* Wed Feb 25 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.0.10-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_11_Mass_Rebuild

* Tue Dec 02 2008 Douglas E. Warner <silfreed@silfreed.net> 2.0.10-1
- update to 2.0.10
- fix for CVE-2008-5110

* Mon Sep 15 2008 Peter Vrabec <pvrabec@redhat.com> 2.0.8-3
- do not conflicts with rsyslog, both rsyslog and syslog-ng use
  same pidfile and logrotate file (#441664)

* Sat Sep  6 2008 Tom "spot" Callaway <tcallawa@redhat.com> 2.0.8-2
- fix license tag

* Thu Jan 31 2008 Douglas E. Warner <silfreed@silfreed.net> 2.0.8-1
- updated to 2.0.8
- removed logrotate patch

* Tue Jan 29 2008 Douglas E. Warner <silfreed@silfreed.net> 2.0.7-2
- added patch from git commit a8b9878ab38b10d24df7b773c8c580d341b22383
  to fix log rotation (bug#430057)

* Tue Jan 08 2008 Douglas E. Warner <silfreed@silfreed.net> 2.0.7-1
- updated to 2.0.7
- force regeneration to avoid broken paths from upstream (#265221)
- adding loggen binary

* Mon Dec 17 2007 Douglas E. Warner <silfreed@silfreed.net> 2.0.6-1
- updated to 2.0.6
- fixes DoS in ZSA-2007-029

* Thu Nov 29 2007 Peter Vrabec <pvrabec@redhat.com> 2.0.5-3
- add conflicts (#400661)

* Wed Aug 29 2007 Fedora Release Engineering <rel-eng at fedoraproject dot org> - 2.0.5-2
- Rebuild for selinux ppc32 issue.

* Thu Jul 26 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.5-1
- Update to 2.0.5

* Thu Jun  7 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.4-4
- Add support for vim 7.1.

* Thu May 31 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.4-3
- Increase the number of unix-stream max-connections (10 -> 32).

* Sat May 26 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.4-2
- New upstream download location
  (https://lists.balabit.hu/pipermail/syslog-ng/2007-May/010258.html)

* Tue May 22 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.4-1
- Update to 2.0.4.

* Mon Mar 26 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.3-1
- Update to 2.0.3.

* Fri Mar 23 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.3-0.20070323
- Update to latest snapshot (2007-03-23).

* Fri Mar  9 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.3-0.20070309
- Enable support for TCP wrappers (--enable-tcp-wrapper).
- Optional support for spoofed source addresses (--enable-spoof-source)
  (disabled by default; build requires libnet).

* Sun Feb 25 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.2-2
- Dynamic link glib2 and eventlog (--enable-dynamic-linking).
  For Fedora Core 6 (and above) both packages install their dynamic
  libraries in /lib.

* Mon Jan 29 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.2-1
- Update to 2.0.2.

* Thu Jan  4 2007 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.1-1
- Update to 2.0.1.

* Fri Dec 15 2006 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.0-1
- Updated the init script patch: LSB Description and Short-Description.

* Fri Nov  3 2006 Jose Pedro Oliveira <jpo at di.uminho.pt> - 2.0.0-0
- Update to 2.0.0.

* Sun Sep 10 2006 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.11-3
- Rebuild for FC6.

* Sun Jun  4 2006 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.11-2
- Install the vim syntax file.

* Fri May  5 2006 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.11-1
- Update to 1.6.11.

* Sun Apr  2 2006 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.10-2
- Build option to support the syslog-ng spoof-source feature
  (the feature spoof-source is disabled by default).

* Thu Mar 30 2006 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.10-1
- Update to 1.6.10.
- The postscript documentation has been dropped (upstream).

* Wed Feb 15 2006 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.9-3
- Rebuild.

* Mon Dec 19 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.9-2
- Provides syslog instead of sysklogd (#172885).

* Wed Nov 30 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.9-1
- Build conflict statement
  (see: https://lists.balabit.hu/pipermail/syslog-ng/2005-June/007630.html)

* Wed Nov 23 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.9-0
- Update to 1.6.9.
- The libol support library is now included in the syslog-ng tarball.

* Wed Jun 22 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.8-2
- BuildRequire which, since it's not part of the default buildgroup
  (Konstantin Ryabitsev).

* Fri May 27 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.8-1
- Update to 1.6.8.

* Thu May 26 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.7-3
- Shipping the sysklogd logrotate file and using the same pidfile
  as suggested by Jeremy Katz.
- Patching the init script: no default runlevels.
- Removed the triggers to handle the logrotate file (no longer needed).
- The SELinux use_syslogng boolean has been dropped (rules enabled).

* Sat May 07 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 1.6.7-2.fc4
- Increased libol required version to 0.3.16
  (https://lists.balabit.hu/pipermail/syslog-ng/2005-May/007385.html).

* Sat Apr 09 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.7-0.fdr.1
- Update to 1.6.7.
- The Red Hat/Fedora Core configuration files are now included in the
  syslog-ng tarball (directory: contrib/fedora-packaging).

* Fri Mar 25 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.6-0.fdr.4
- Logrotate file conflict: just comment/uncomment contents of the syslog
  logrotate file using triggers.

* Tue Feb 15 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.6-0.fdr.3
- Require logrotate.
- Documentation updates (upstream).

* Sat Feb 05 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.6-0.fdr.2
- Added contrib/relogger.pl (missing in syslog-ng-1.6.6).
- Requires libol 0.3.15.
- Added %%trigger scripts to handle the logrotate file.

* Fri Feb 04 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.6-0.fdr.1
- Update to 1.6.6.
- Patches no longer needed.

* Fri Feb 04 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.5-0.fdr.7
- Took ownership of the configuration directory (/etc/syslog-ng/).
- Updated the syslog-ng(8) manpage (upstream patch).
- Updated the configuration file: /proc/kmsg is a file not a pipe.
- Patched two contrib files: syslog2ng and syslog-ng.conf.RedHat.
- Logrotate file inline replacement: perl --> sed (bug 1332 comment 23).

* Tue Jan 25 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.5-0.fdr.6
- Logrotate problem: only one logrotate file for syslog and syslog-ng.
- Configuration file: don't sync d_mail destination (/var/log/maillog).

* Mon Jan 24 2005 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.5-0.fdr.5
- SIGHUP handling upstream patch (syslog-ng-1.6.5+20050121.tar.gz).
- Static linking /usr libraries (patch already upstream).

* Thu Sep 30 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> - 0:1.6.5-0.fdr.4
- make: do not strip the binaries during installation (install vs install-strip)
  (bug 1332 comment 18).
- install: preserve timestamps (option -p) (bug 1332 comment 18).

* Wed Sep  1 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.5-0.fdr.3
- use the tcp_wrappers static library instead (bug 1332 comment 15).

* Wed Sep  1 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.5-0.fdr.2
- added missing build requirement: flex (bug 1332 comment 13).

* Wed Aug 25 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.5-0.fdr.1
- update to 1.65.
- removed the syslog-ng.doc.patch patch (already upstream).
- removed the syslog-ng.conf.solaris documentation file.

* Wed Apr 21 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.2-0.fdr.3
- removed Conflits:
- changed the %%post and %%preun scripts
- splitted Requires( ... , ... ) into Requires( ... )

* Fri Mar  5 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.2-0.fdr.2
- corrected the source URL

* Sat Feb 28 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.2-0.fdr.1
- changed packaged name to be compliant with fedora.us naming conventions

* Fri Feb 27 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.2-0.fdr.0.2
- updated to version 1.6.2
- undo "Requires: tcp_wrappers" - tcp_wrappers is a static lib

* Sat Feb  7 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.1-0.fdr.2
- make %{?_smp_mflags}
- Requires: tcp_wrappers

* Sat Jan 10 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 0:1.6.1-0.fdr.1
- first release for fedora.us

* Fri Jan  9 2004 Jose Pedro Oliveira <jpo at di.uminho.pt> 1.6.1-1.1tux
- updated to version 1.6.1

* Tue Oct  7 2003 Jose Pedro Oliveira <jpo at di.uminho.pt> 1.6.0rc4-1.1tux
- updated to version 1.6.0rc4

* Tue Aug 26 2003 Jose Pedro Oliveira <jpo at di.uminho.pt> 1.6.0rc3-1.4tux
- installation scripts improved
- conflits line

* Sat Aug 16 2003 Jose Pedro Oliveira <jpo at di.uminho.pt> 1.6.0rc3-1.3tux
- install-strip

* Tue Jul 22 2003 Jose Pedro Oliveira <jpo at di.uminho.pt> 1.6.0rc3-1.2tux
- missing document: contrib/syslog-ng.conf.doc

* Thu Jun 12 2003 Jose Pedro Oliveira <jpo at di.uminho.pt> 1.6.0rc3-1.1tux
- Version 1.6.0rc3

* Sat Apr 12 2003 Jose Pedro Oliveira <jpo at di.uminho.pt> 1.6.0rc2 snapshot
- Reorganized specfile
- Corrected the scripts (%%post, %%postun, and %%preun)
- Commented the mysql related lines; create an option for future inclusion

* Thu Feb 27 2003 Richard E. Perlotto II <richard@perlotto.com> 1.6.0rc1-1
- Updated for new version

* Mon Feb 17 2003 Richard E. Perlotto II <richard@perlotto.com> 1.5.26-1
- Updated for new version

* Fri Dec 20 2002 Richard E. Perlotto II <richard@perlotto.com> 1.5.24-1
- Updated for new version

* Fri Dec 13 2002 Richard E. Perlotto II <richard@perlotto.com> 1.5.23-2
- Corrected the mass of errors that occured with rpmlint
- Continue to clean up for the helpful hints on how to write to a database

* Sun Dec 08 2002 Richard E. Perlotto II <richard@perlotto.com> 1.5.23-1
- Updated file with notes and PGP signatures

# vim:set ai ts=4 sw=4 sts=4 et:
