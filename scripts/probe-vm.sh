#!/bin/sh
# USB hotplug and IOService::start are asynchronous; wait for a completed probe.
set -eu
cd "$(dirname "$0")/.."
exec scripts/vm-ssh.sh '
    count=0
    while test "$count" -lt 360; do
        /Users/builder/dwa121/native/build/dwa121-info > /Users/builder/dwa121/native/build/probe-last.plist 2> /Users/builder/dwa121/native/build/probe-last.err
        code=$?
        if test "$code" = 0; then
            cat /Users/builder/dwa121/native/build/probe-last.plist
            exit 0
        fi
        if test "$code" != 2; then
            cat /Users/builder/dwa121/native/build/probe-last.err >&2
            exit "$code"
        fi
        count=`expr "$count" + 1`
        sleep 1
    done
    echo "Timed out waiting for DWA121Diagnostic; inspect dmesg." >&2
    exit 1
'
