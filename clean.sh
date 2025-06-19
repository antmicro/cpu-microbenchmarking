#!/usr/bin/env bash
set -eu

dry_run=${1:-}

function remove() {
    
    if [ -z "$dry_run" ]; then
        git_dry_run=""
    else
        git_dry_run="-n"  # The argument to make git-clean do a dry-run
    fi

    for path in "$@"; do 
        # Use git clean instead of rm to avoid deleting tracked files that match patterns.
        git clean -xff ${git_dry_run} ${path}  # -ff required to delete submodules
    done
}

# Compiled binaries
remove ./cpu-benchmark*

# Cloned repos
remove ./tlib_*

# Build artifacts
remove ./*.o

