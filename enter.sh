#!/usr/bin/env bash

is_sourced() {
  if [ -n "$ZSH_VERSION" ]; then 
      case $ZSH_EVAL_CONTEXT in *:file:*) return 0;; esac
  else  # Add additional POSIX-compatible shell names here, if needed.
      case ${0##*/} in dash|-dash|bash|-bash|ksh|-ksh|sh|-sh) return 0;; esac
  fi
  return 1  # NOT sourced.
}

if ! is_sourced; then
	echo "This script needs to be sourced, not run. Source it with '. $0'"
	exit 1
fi

uv venv --allow-existing
uv pip install west
. ./.venv/bin/activate
