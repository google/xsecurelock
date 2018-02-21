#!/bin/sh

set -e

xwininfo -root

# Set up an isolated homedir with a fixed password.
homedir=$(mktemp -d -t xsecurelock-run-test.XXXXXX)
trap 'rm -rf "$homedir"' EXIT
htpasswd -bc "$homedir/.xsecurelock.pw" "$USER" hunter2

# Run preparatory commands.
eval "$(grep '^#preexec ' "$1" | cut -d ' ' -f 2-)"

# Lock the screen - and wait for the lock to succeed.
mkfifo "$homedir"/lock.notify
HOME="$homedir" XSECURELOCK_AUTH=auth_htpasswd \
  xsecurelock -- cat "$homedir"/lock.notify & pid=$!
echo "Waiting for lock..."
: > "$homedir"/lock.notify
echo "Locked."

# Run the test script.
set +e
xdotool - < "$1"
result=$?
set -e

# Kill the lock, if remaining.
kill "$pid" || true

# Finish the test.
echo "Test $1 status: $result."
