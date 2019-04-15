% XSECURELOCK(1) XSecureLock User Manual
% Rudolf Polzer
% April 15, 2019

# NAME

XSecureLock - X11 screen lock utility

# SYNPOSIS

[*options*] xsecurelock [-- *command-to-run-when-locked*]

# DESCRIPTION

XSecureLock is an X11 screen lock utility designed with the primary goal of
security.

It locks the current X11 session, and only allows unlocking if the user
authenticates to it (typically with the login password).

While locked, it can launch a screen saver process and then waits for
input events. Upon an input event, it displays a login dialog to allow
for unlocking.

# OPTIONS

Options are set as environment variables prior to invoking XSecureLock;
the following variables are available:

<!-- ENV VARIABLES HERE -->

Additionally, XSecureLock spawns the *command-to-run-when-locked* once locking
is complete; this can be used as a notification mechanism if desired.

# REPORTING BUGS

The official bug tracker is at <https://github.com/google/xsecurelock/issues/>.

# COPYRIGHT

The code is released unser the Apache 2.0 license. See the LICENSE file for more
details.

# SEE ALSO

`xss-lock` (1), `xautolock` (1).

The *README.md* file included with XSecureLock contains full documentation.

The XSecureLock source  code and all documentation may be downloaded on
<https://github.com/google/xsecurelock/>.
