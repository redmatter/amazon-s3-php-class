#!/bin/bash

_indent() {
	local count="$1"; shift;
	local indent_with="${1:-    }"; shift;
}

option_filter='--log.|--help|--version|--temp-dir'
CommonOptions() {
    archiving-cli-convert-to-clear --help | egrep -- $option_filter
}

SubcommandUsage() {
	local cmd="archiving-cli-$1";
	which $cmd &>/dev/null &&
    	archiving-cli-$1 --help | egrep -v -- $option_filter
}

Usage() {
	cat <<USAGE
Archiving CLI
(C) 2016 Red Matter Ltd.

USAGE:
archiving-cli <sub-command> <options>

COMMON OPTIONS:
Below options apply to all sub-commands.
$(CommonOptions) 

SUB-COMMANDS:
$(SubcommandUsage convert-to-clear) 

$(SubcommandUsage convert-to-archive) 

$(SubcommandUsage download) 

help - displays this help

USAGE
}

subcommand="$1"; shift;
case "$subcommand" in
	convert-to-archive|convert-to-clear|download)
		exec archiving-cli-$subcommand "$@";
		;;
	help)
		Usage;
		;;
	*)
		echo "ERROR: Unknown subcommand $subcommand";
		Usage;
		;;
esac
