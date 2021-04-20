# Balabit-Europe Kft. 2019-2020. (c) All rights reserved.
# One Identity Hungary Kft. 2020-2021. (c) All rights reserved.

# Licensing and use governed by the GNU GPL version 2 license


query_install_path() {
	echo -n "Please enter your installation path for Syslog-ng PE: [${INSTALL_PATH}] "
	read INPUT <"${CONSOLE_TTY}"
}


check_dir() {
	if [ -d "${1}" ]; then
		return 0
	else
		echo "The directory you specified does not exist!" >&2
		return 1
	fi
}


verify_input() {
	INPUT="${INPUT:-${INSTALL_PATH}}"
	echo -n "You have entered '${INPUT}'. Is this correct? [y/N] "
	read ACCEPT <"${CONSOLE_TTY}"
	if [ "x${ACCEPT}x" != "xyx" ]; then return 0; fi
	check_dir "${INPUT}" && return 1 || return 0
}


extract_version_string() {
	sed -n 's:^[a-zA-Z ]\+\([0-9]\+\.[0-9]\+\)\(.[0-9]\+\)\?[a-zA-Z ()]\+$:\1:p'
}


detect_os_version() {
	echo "Detecting RHEL/CentOS/Oracle Linux version..."
	if [ -x "/usr/bin/lsb_release" ]; then
		if lsb_release -d | grep -qE "Description:[[:space:]]+(CentOS|CentOS Linux|Red Hat Enterprise Linux Server|Oracle Linux Server|Enterprise Linux Enterprise Linux Server) release"; then
			OS_VERSION=$( lsb_release -r | cut -f 2 )
		else
			echo "You don't seem to be running a supported Linux distribution!" >&2
			exit 250
		fi
	else
		# The package redhat-lsb-core is most likely not installed...
		if [ -f "/etc/redhat-release" ]; then
			OS_VERSION=$( extract_version_string < "/etc/redhat-release" )
		else
			echo "You don't seem to be running a supported OS!" >&2
			exit 250
		fi
	fi
}


get_syslog_binary() {
	if is_available syslog-ng; then
		SYSLOG_BINARY=$( which syslog-ng 2>/dev/null )
	elif [ -x "${INSTALL_PATH}/sbin/syslog-ng" ]; then
		SYSLOG_BINARY="${INSTALL_PATH}/sbin/syslog-ng"
	fi
}


syslog_ng_is_pe() {
	if "${SYSLOG_BINARY}" -V 2>/dev/null | fgrep -q premium-edition; then
		return 0
	else
		return 1
	fi
}


syslog_ng_is_ose() {
	if "${SYSLOG_BINARY}" -V 2>/dev/null | fgrep -q premium-edition; then
		return 1
	else
		return 0
	fi
}


get_syslog_ng_version() {
	SYSLOG_VERSION=$( "${SYSLOG_BINARY}" -V 2>/dev/null | grep -E "^syslog-ng.*\(.*\)$" | sed 's:^syslog-ng.*(\([0-9]\+\)\.\([0-9]\+\)\(\.\([0-9a-z]\+\)\)\?)$:\1.\2\3:' )
}


omit_allowed_ports() {
	SYSLOG_NG_TCP_PORTS=$( omit_allowed_tcp_ports <<<"${SYSLOG_NG_TCP_PORTS}" )
	SYSLOG_NG_UDP_PORTS=$( omit_allowed_udp_ports <<<"${SYSLOG_NG_UDP_PORTS}" )
}


remove_trailing_slash() {
	# the trailing slash in the install path (if present) breaks file context rules
	# thus it needs to be removed (provided that the install path is not "/" itself)
	sed -e 's:^\(.\+\)/$:\1:'
}


check_reply() {
	if [ "x${CONFIRM_REPLY}x" == "xyx" ]; then
		return 1
	elif [ "x${CONFIRM_REPLY}x" == "xnx" -o "x${CONFIRM_REPLY}x" == "xx" ]; then
		return 1
	else
		echo "Invalid reply: \'${CONFIRM_REPLY}\'!" >&2
		return 0
	fi
}


is_available () {
	which "$1" >/dev/null 2>&1;
}


syslog_ng_is_not_installed() {
	if is_available syslog-ng; then
		return 1
	elif [ -x "${INSTALL_PATH}/sbin/syslog-ng" ]; then
		return 1
	else
		return 0
	fi
}


get_console_tty() {
	if is_available tty; then
		CONSOLE_TTY=$( tty )
	else
		echo "The 'tty' binary is not available!" >&2
		exit 244
	fi
}


remove_trainling_slash() {
        # the trailing slash in the install path (if present) breaks file context rules
        # thus it needs to be removed (provided that the install path is not "/" itself)
        sed -e 's:^\(.\+\)/$:\1:'
}