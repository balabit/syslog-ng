#!/bin/bash

# Balabit-Europe Kft. 2019-2020. (c) All rights reserved.
# One Identity Hungary Kft. 2020-2021. (c) All rights reserved.

# Licensing and use governed by the GNU GPL version 2 license

### Exit error codes
#
# 0: everything went fine
# 255: invalid command line-option
# 254: no root privileges
# 253: file or directory does not exist
# 252: policy build failed
# 251: policy install failed
# 250: unsupported os/distribution/version
# 249: conflicting parameters
# 248: internal error at task selection
# 247: internal error at callback task selection
# 246: syslog-ng not installed
# 245: invalid plugin metadata
# 244: tty not available
# 243: invalid plugin encountered #1
# 242: invalid plugin encountered #2
# 241: invalid plugin encountered #3
# 240: include file cannot be loaded

cd $( dirname $( readlink -f "${0}" ) )
if [ -f include/common.sh.inc ]; then
    . include/common.sh.inc
else
    echo "Cannot find the include file 'include/common.sh.inc'!" >&2
    exit 240
fi

EL_FC=
EL_TE=
OS_VERSION=
SYSLOG_VERSION=
SYSLOG_BINARY=
CONSOLE_TTY=
CONFIRM_REPLY=
WITH_PLUGINS="ask"
INSTALL_PATH="/opt/syslog-ng"
PLUGIN_DB=
# RHEL8 note: ports 10514/tcp, 10514/udp, 20514/tcp and 20514/udp have been
#  allowed by default
#
# Post-RHEL6.5 note: ports 514/udp, 6514/udp and 6514/tcp are allowed by default
#  if you wish to add further ports, just add them to the end of the list
SYSLOG_NG_TCP_PORTS="601"
SYSLOG_NG_UDP_PORTS="601"
TASK_SELECTED="install_default"
INPUT=

PLUGIN_NAME=
PLUGIN_FEATURE=
PE_SUPPORT_ADDED_VERSION=
OSE_SUPPORT_ADDED_VERSION=


omit_allowed_tcp_ports() {
	sed -e 's:^601::g'
}


omit_allowed_udp_ports() {
	sed -e 's:^601::g'
}


substitute_install_path() {
	sed -e "s:^\\\$PATH\\\$:${INSTALL_PATH}:g" "core/root_unsafe/${EL_FC}"
	sed -e "s:^\\\$PATH\\\$:${INSTALL_PATH}:g" "core/root_safe/${EL_FC}"
}


omit_install_path() {
	sed -e "s:^\\\$PATH\\\$::g" "core/root_safe/${EL_FC}"
}


setup_vars() {
	get_syslog_binary
	get_syslog_ng_version

	echo "Detected RHEL/CentOS/Oracle Linux ${OS_VERSION}."
	case "${OS_VERSION}" in
		5.*)

			EL_FC="syslog_ng.el5.fc.in"
			EL_TE="syslog_ng.el5.te.in"
			;;
		6.*)
			EL_FC="syslog_ng.el6.fc.in"

			local MINORVER 
			MINORVER=$( cut -d. -f 2 <<<"${OS_VERSION}" )
			if [ "${MINORVER}" -lt 5 ]; then
				EL_TE="syslog_ng.el6.0to4.te.in"
			else
				EL_TE="syslog_ng.el6.5up.te.in"
				    
				# 601/tcp and 601/udp are allowed by default on RHEL6.5+, so there is no need to enable them
				omit_allowed_ports
			fi
			;;
		7.*)
			EL_FC="syslog_ng.el78.fc.in"
			EL_TE="syslog_ng.el7.te.in"

			# 601/tcp and 601/udp are allowed by default on RHEL7, so there is no need to enable them
			omit_allowed_ports
			;;
		8.*)
			EL_FC="syslog_ng.el78.fc.in"
			EL_TE="syslog_ng.el8.te.in"

			# 601/tcp and 601/udp are allowed by default on RHEL8, so there is no need to enable them
			omit_allowed_ports
			;;
		*)
			echo "You don't seem to be running a supported version of RHEL!" >&2
			exit 250
			;;
	esac
}


prepare_files() {
	echo "Using '${INSTALL_PATH}'..." 
	if [ "${INSTALL_PATH}" != "/" ]; then
		substitute_install_path > "syslog_ng.fc"
	else
		omit_install_path > "syslog_ng.fc"
	fi
	cat "core/syslog_ng.module.version" "core/${EL_TE}" > "syslog_ng.te"
}

dump_PLUGIN_DB() {
# workaround to ensure every newline gets where it belongs
# note: fgrep caused havoc when PLUGIN_DB was presented to it as stdin
	echo -e "${PLUGIN_DB}"
}


fetch_plugin_dir() {
# PLUGIN_DB format is "name:choice"
	while read LINE; do [ -n "${LINE}" ] && echo "${LINE%%:*}"; done
}

fetch_plugin_choice() {
# PLUGIN_DB format is "name:choice"
	while read LINE; do [ -n "${LINE}" ] && echo -ne "${LINE##*:}"; done
}

get_plugin_list() {
	while IFS= read -r -d $'\0' plugin_dir; do
		echo "${plugin_dir}"
	done < <( find plugins -maxdepth 1 -type d -a ! -name plugins -print0 | sort -zn )
}


init_plugin_db() {
	PLUGIN_DB=$( get_plugin_list | sed 's/$/:/g' )
}


query_plugins() {
# Metadata variables for the current plugin
# 
# PLUGIN_NAME: name of the module that the plugin adds support for
# PLUGIN_FEATURE: possible values: ALL, PE_ONLY, OSE_ONLY, NONE
# PE_SUPPORT_ADDED_VERSION: string describing version of PE where the feature
#   was added
# OSE_SUPPORT_ADDED_VERSION: string describing version of OSE where the feature
#   was added
	if [ "${WITH_PLUGINS}" == "ask" ]; then
		local P_DB_SCRATCH_BUF=
		while read plugin_dir; do
			PLUGIN_NAME=
			PLUGIN_FEATURE=
			PE_SUPPORT_ADDED_VERSION=
			OSE_SUPPORT_ADDED_VERSION=

			source "${plugin_dir}/include/include.sh"
			validate_plugin_metadata "${plugin_dir}" "${PLUGIN_NAME}" "${PLUGIN_FEATURE}" "${PE_SUPPORT_ADDED_VERSION}" "${OSE_SUPPORT_ADDED_VERSION}"
			print_plugin_info "${PLUGIN_NAME}" "${PLUGIN_FEATURE}" "${PE_SUPPORT_ADDED_VERSION}" "${OSE_SUPPORT_ADDED_VERSION}"

			ask_plugin_install "${PLUGIN_NAME}"
			read CONFIRM_REPLY <"${CONSOLE_TTY}"
			while check_reply; do
			ask_plugin_install "${PLUGIN_NAME}"
				read CONFIRM_REPLY <"${CONSOLE_TTY}"
			done

			if [ "x${CONFIRM_REPLY}x" == "xyx" -o "x${CONFIRM_REPLY}x" == "xYx" ]; then
				P_DB_SCRATCH_BUF="${plugin_dir}:Y\n${P_DB_SCRATCH_BUF}"
			else
				P_DB_SCRATCH_BUF="${plugin_dir}:N\n${P_DB_SCRATCH_BUF}"
			fi
		done < <( dump_PLUGIN_DB | fetch_plugin_dir )
		PLUGIN_DB="${P_DB_SCRATCH_BUF}"
	elif [ "${WITH_PLUGINS}" == "all" ]; then
		PLUGIN_DB=$( dump_PLUGIN_DB | sed 's/:$/:Y/g' )
	else
		PLUGIN_DB=$( dump_PLUGIN_DB | sed 's/:$/:N/g' )
	fi
}


ask_plugin_install() {
	echo -n "You are about to install the SELinux policy module for '${1}'. Do you wish to proceed? [y/N] "
}


print_plugin_info() {
	local NAME FEATURE 
	NAME="${1}"
	FEATURE="${2}"
	[ -n "${3}" ] && PE_COMP_VER="${3} and up" || PE_COMP_VER="NONE" 
	[ -n "${4}" ] && OSE_COMP_VER="${4} and up" || OSE_COMP_VER="NONE" 


	echo -ne "Plugin summary:\nName:\t\t\t\t\t\t${NAME}\nFeature:\t\t\t\t\t"
	case "${FEATURE}" in
		"ALL")
			echo "For all versions"
			;;
		"NONE")
			echo "Disabled for all versions"
			;;
		"PE_ONLY")
			echo "Premium Edition only"
			;;
		"OSE_ONLY")
			echo "Open-Source Edition only"
			;;
		*)
			echo "INVALID"
			;;
	esac
	echo -ne "Premium Edition compatible versions:\t\t${PE_COMP_VER}\nOpen-Source Edition compltible versions:\t${OSE_COMP_VER}"

	echo -ne "\n\nInstalled Syslog-ng instance:\nEdition:\t\t\t\t\t" 
	if syslog_ng_is_pe; then
		echo "Premium Edition"
	elif syslog_ng_is_ose; then
		echo "Open-Source Edition"
	else
		echo "Unknown"
	fi
	echo -e "Version:\t\t\t\t\t${SYSLOG_VERSION}\n" 
}


plugin_install_precheck() {
	echo -ne "\nPlugin constraint evaluation result:\t\t"
	if plugin_check_feature "${1}" && plugin_check_compatibility "${2}" "${3}"; then
		echo "PASS"
		return 0
	else
		echo "SKIP"
		return 1
	fi
}


plugin_check_feature() {
	case "${1}" in
		"ALL")
			return 0
			;;
		"NONE")
			return 1
			;;
		"PE_ONLY")
			if syslog_ng_is_pe; then
				return 0
			else
				return 1
			fi
			;;
		"OSE_ONLY")
			if syslog_ng_is_ose; then
				return 0
			else
				return 1
			fi
			;;
		*)
			echo "Invalid plugin metadata PLUGIN_FEATURE: '${1}'!" >&2
			exit 245
			;;
	esac
}


plugin_version_is_greater_than() {
	local OUR_VER REF_VER
	OUR_VER="${1}"
	REF_VER="${2}"

	IFS="." read O_MAJOR O_MINOR O_REVISION <<<"${OUR_VER}"
	IFS="." read R_MAJOR R_MINOR R_REVISION <<<"${REF_VER}"

	if (( $O_MAJOR > $R_MAJOR )); then
		return 0
	elif (( $O_MAJOR == $R_MAJOR )); then
		if (( $O_MINOR > $R_MINOR )); then
			return 0
		elif (( $O_MINOR == $R_MINOR )); then
			if [ -n "${O_MINOR}" -a -n "${R_MINOR}" ]; then
				if (( $O_REVISION > $R_REVISION )); then
					return 0
				elif (( $O_REVISION == $R_REVISION )); then
					return 0
				else
					return 1
				fi
			elif [ -z "${O_MINOR}" -a -z "${R_MINOR}" ]; then
				return 0
			elif [ -n "${O_MINOR}" -a -z "${R_MINOR}" ]; then
				return 0
			else
				return 1
			fi
		else
			return 1
		fi
	else
		return 1
	fi
}


plugin_check_compatibility() {
	local PE_VER OSE_VER
	PE_SUPPORTED_VER="${1}"
	OSE_SUPPORTED_VER="${2}"

	if syslog_ng_is_pe; then
		if plugin_version_is_greater_than "${SYSLOG_VERSION}" "${PE_SUPPORTED_VER}" ; then
			return 0
		else
			return 1
		fi
	elif syslog_ng_is_ose; then
		if plugin_version_is_greater_than "${SYSLOG_VERSION}" "${OSE_SUPPORTED_VER}"; then
			return 0
		else
			return 1
		fi
	else
		return 1
	fi
}


validate_plugin_metadata() {
	local plugin_dir PLUGIN_NAME PLUGIN_FEATURE PE_SUPPORT_ADDED_VERSION OSE_SUPPORT_ADDED_VERSION

	plugin_dir="${1}"
	PLUGIN_NAME="${2}"
	PLUGIN_FEATURE="${3}"
	PE_SUPPORT_ADDED_VERSION="${4}"
	OSE_SUPPORT_ADDED_VERSION="${5}"

	if [[ ( -z "${PLUGIN_NAME}" || -z "${PLUGIN_FEATURE}" ) || ( -z "${PE_SUPPORT_ADDED_VERSION}" && -z "${OSE_SUPPORT_ADDED_VERSION}" ) ]]; then
		echo "Invalid plugin was encountered in directory: '${plugin_dir}'!" >&2
		exit 243 
	elif [[ ( "${PLUGIN_FEATURE}" == "PE_ONLY" && -z "${PE_SUPPORT_ADDED_VERSION}" ) || ( "${PLUGIN_FEATURE}" == "OSE_ONLY" && -z "${OSE_SUPPORT_ADDED_VERSION}" ) ]]; then
		echo "Invalid plugin was encountered in directory: '${plugin_dir}'!" >&2
		exit 242
	elif [[ ( "${PLUGIN_FEATURE}" == "ALL" ) && ( -z "${PE_SUPPORT_ADDED_VERSION}" || -z "${OSE_SUPPORT_ADDED_VERSION}" ) ]]; then
		echo "Invalid plugin was encountered in directory: '${plugin_dir}'!" >&2
		exit 241
	fi
}


run_plugin_installers() {
	local choice

	while read plugin_dir; do
		PLUGIN_NAME=
		PLUGIN_FEATURE=
		PE_SUPPORT_ADDED_VERSION=
		OSE_SUPPORT_ADDED_VERSION=

		source "${plugin_dir}/include/include.sh"
		validate_plugin_metadata "${plugin_dir}" "${PLUGIN_NAME}" "${PLUGIN_FEATURE}" "${PE_SUPPORT_ADDED_VERSION}" "${OSE_SUPPORT_ADDED_VERSION}"

		choice=$( echo -ne "${PLUGIN_DB}" | fgrep "${plugin_dir}:" | fetch_plugin_choice )
		if [ "x${choice}x" == "xYx" ] ; then
			if plugin_install_precheck "${PLUGIN_FEATURE}" "${PE_SUPPORT_ADDED_VERSION}" "${OSE_SUPPORT_ADDED_VERSION}"; then
				if already_loaded; then
					echo "The SELinux module for '${PLUGIN_NAME}' is already loaded! Skipping install..." >&2
				else
					"${plugin_dir}/setup.sh" --install-dir "${INSTALL_PATH}" --os "${OS_VERSION}"
				fi
			fi
		fi
	done < <( dump_PLUGIN_DB | fetch_plugin_dir )
}


run_plugin_callbacks() {
	local choice buffer
	while read plugin_dir; do
		PLUGIN_NAME=
		PLUGIN_FEATURE=
		PE_SUPPORT_ADDED_VERSION=
		OSE_SUPPORT_ADDED_VERSION=

		source "${plugin_dir}/include/include.sh"
		validate_plugin_metadata "${plugin_dir}" "${PLUGIN_NAME}" "${PLUGIN_FEATURE}" "${PE_SUPPORT_ADDED_VERSION}" "${OSE_SUPPORT_ADDED_VERSION}"

		choice=$( dump_PLUGIN_DB | fgrep "${plugin_dir}:" | fetch_plugin_choice )
		if [ "x${choice}x" == "xYx" ] ; then
			for type in te if fc; do
				buffer=$( return_files "${type}" )
				[ -n "${buffer}" ] && cat >> "syslog_ng.${type}" <<<"${buffer}"
			done
		fi
	done < <( dump_PLUGIN_DB | fetch_plugin_dir )
}


remove_plugin_modules() {
	while read plugin_dir; do
		PLUGIN_NAME=
		PLUGIN_FEATURE=
		PE_SUPPORT_ADDED_VERSION=
		OSE_SUPPORT_ADDED_VERSION=

		source "${plugin_dir}/include/include.sh"
		validate_plugin_metadata "${plugin_dir}" "${PLUGIN_NAME}" "${PLUGIN_FEATURE}" "${PE_SUPPORT_ADDED_VERSION}" "${OSE_SUPPORT_ADDED_VERSION}"

		if already_loaded; then
			"${plugin_dir}/setup.sh" --remove --os "${OS_VERSION}"
		else
			echo "No installed ${PLUGIN_NAME} SELinux policy module was found. No removal is necessary. Skipping..."
		fi
	done < <( dump_PLUGIN_DB | fetch_plugin_dir )
}


filter_bogus_build_output() {
	#filter misleading output caused by RHEL bug 1861968
	fgrep -v /usr/share/selinux/devel/include/services/container.if
}


build_module() {
	echo "Building and Loading Policy"
	build_output=$( make -f /usr/share/selinux/devel/Makefile syslog_ng.pp 2>&1 )
	retval=${?}
	filter_bogus_build_output <<<"${build_output}"
	[ ${retval} -eq 0 ] || exit 252
}


add_ports() {
	for entry in ${@}; do
		port=${entry%/*}
		proto=${entry#*/}
		semanage port -a -t syslogd_port_t -p ${proto} ${port} 2>/dev/null || \
		semanage port -m -t syslogd_port_t -p ${proto} ${port} 2>/dev/null
	done
}


install_precheck() {
	if syslog_ng_is_not_installed; then
		echo "Syslog-ng does not seem to be installed!" >&2
		exit 246
	fi
}


install_module() {
	if /usr/sbin/semodule -l | grep -qw syslog_ng; then
		echo "The Syslog-ng SELinux policy module is already installed. Nothing to do..."
		echo "If it belongs to a previous version, then you will have to remove it first."
	else
		/usr/sbin/semodule -i syslog_ng.pp -v || exit 251

		# set up syslog-ng specific ports
		PORTS=
		for port in ${SYSLOG_NG_TCP_PORTS}; do PORTS="${PORTS} ${port}/tcp"; done
		for port in ${SYSLOG_NG_UDP_PORTS}; do PORTS="${PORTS} ${port}/udp"; done
		add_ports "${PORTS}"

		# Fixing the file context
		/sbin/restorecon -F -Rv "${INSTALL_PATH}"
		[ -f /etc/init.d/syslog-ng ] && /sbin/restorecon -F -v /etc/init.d/syslog-ng
		[ -f /etc/rc.d/init.d/syslog-ng ] && /sbin/restorecon -F -v /etc/rc.d/init.d/syslog-ng
		/sbin/restorecon -F -Rv /dev/log

		echo -e "\nInstallation of the Syslog-ng SELinux policy module finished.\nPlease restart syslog-ng. You can find more information about this in the README file."
	fi
}


remove_ports() {
	for entry in ${@}; do
		port=${entry%/*}
		proto=${entry#*/}
		semanage port -d -t syslogd_port_t -p ${proto} ${port} 2>/dev/null
	done
}


remove_module() {
	if /usr/sbin/semodule -l | grep -qw syslog_ng; then
		echo -n "Removing Syslog-ng SELinux policy module... "

		/usr/sbin/semodule --remove=syslog_ng

		# unconfigure syslog-ng specific ports
		PORTS=
		for port in ${SYSLOG_NG_TCP_PORTS}; do PORTS="${PORTS} ${port}/tcp"; done
		for port in ${SYSLOG_NG_UDP_PORTS}; do PORTS="${PORTS} ${port}/udp"; done
		remove_ports "${PORTS}"

		[ -f syslog_ng.pp ] && rm -f syslog_ng.pp
		[ -f syslog_ng.te ] && rm -f syslog_ng.te
		[ -f syslog_ng.fc ] && rm -f syslog_ng.fc
		[ -f syslog_ng.if ] && rm -f syslog_ng.if
		[ -d tmp ] && rm -Rf tmp

		echo "done."
	else
		echo "No installed Syslog-ng SELinux policy module was found. No removal is necessary. Skipping..."
	fi
}


CMD=$( basename "${0}" )
USAGE="Usage: ${CMD}\t[ --install-dir <DIRECTORY> | --remove | --help | --without-plugins | --with-all-plugins ]\n\n${CMD}:\tA tool for building and managing the SELinux policy for the\n\t\tdefault syslog-ng installation."


while [ -n "${1}" ]; do
	case "${1}" in
		--help)
			# if --help is supplied, the help message will be printed independently of any other options being specified
			TASK_SELECTED="showhelp"
			break
			;;
		--install-dir)
			[ "${TASK_SELECTED}" = "remove" ] && echo -e "ERROR: Conflicting options!\n\n${USAGE}" >&2 && exit 249
			TASK_SELECTED="install"
			check_dir "${2}" || exit 253
			INPUT="${2}"
			shift 2
			;;
		--remove)
			[ "${TASK_SELECTED}" = "install" ] && echo -e "ERROR: Conflicting options!\n\n${USAGE}" >&2 && exit 249
			TASK_SELECTED="remove"
			shift
			;;
		--without-plugins)
			WITH_PLUGINS="none"
			shift
			;;
		--with-all-plugins)
			WITH_PLUGINS="all"
			shift
			;;
		*)
			echo -e "ERROR: Invalid option: '${1}'\n${USAGE}" >&2
			exit 255
			;;
	esac

done

case "${TASK_SELECTED}" in
	showhelp)
		echo -e "${USAGE}"
		exit 0
		;;
	remove)
		detect_os_version
		setup_vars
		remove_module
		init_plugin_db
		remove_plugin_modules
		exit 0
		;;
	install|install_default)
		if [ $( id -u ) != 0 ]; then
			echo 'You must be root to run this script!' >&2
			exit 254
		fi

		get_console_tty

		if [ -z "${INPUT}" ]; then 
			query_install_path
			while verify_input; do
				query_install_path
			done
		fi

		INSTALL_PATH=$( remove_trailing_slash <<<"${INPUT}" )

		detect_os_version
		install_precheck
		setup_vars
		init_plugin_db
		query_plugins
		run_plugin_installers
		prepare_files
		run_plugin_callbacks
		build_module
		install_module
		;;
	*)
		echo -e "ERROR: Invalid task: '${TASK_SELECTED}'!" >&2
		exit 248
		;;
esac
