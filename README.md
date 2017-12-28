# About XSecureLock

XSecureLock is an X11 screen lock utility designed with the primary goal of
security.

Screen lock utilities are widespread. However, in the past they often had
security issues regarding authentication bypass (a crashing screen locker would
unlock the screen), information disclosure (notifications may appear on top of
the screen saver), or sometimes even worse.

In XSecureLock, security is achieved using a modular design to avoid the usual
pitfalls of screen locking utility design on X11. Details are available in the
[Security Design](#security-design) section.

# Requirements

The following packages need to be installed; their names depend on your Linux
distribution of choice, but will be similar:

* apache2-utils (for the `auth_htpasswd` module)
* automake
* binutils
* gcc
* libc6-dev
* libpam-dev (for the `auth_pam_x11` module)
* libx11-dev
* libxss-dev
* make
* mplayer (for the `saver_mplayer` module)
* mpv (for the `saver_mpv` module)
* imagemagick (for the `auth_htpasswd` and `auth_pamtester` modules)
* pamtester (for the `auth_pamtester` module)
* x11-xserver-utils (for the `saver_blank` module)
* xscreensaver (for the `saver_xscreensaver` module)

# Installation

NOTE: In these instructions, please replace SERVICE-NAME by the name of an
appropriate and existing file in `/etc/pam.d`.
If xscreensaver is installed, `xscreensaver` should always be a good choice;
otherwise, on Debian and Ubuntu, `common-auth` would work. This will be used as
default and can be overridden with [`XSECURELOCK_PAM_SERVICE`](#options).

Configuring a broken or missing SERVICE-NAME will render unlocking the screen
impossible! If this should happen to you, switch to another terminal
(`Ctrl-Alt-F1`), log in there, and run: `killall xsecurelock` to force unlocking
of the screen.

```
git clone https://github.com/google/xsecurelock.git
cd xsecurelock
sh autogen.sh
./configure --with-pam-service-name=SERVICE-NAME
make
make install
```

# Setup

Pick one of the [authentication modules](#authentication-modules) and one of
the [screen saver modules](#screen-saver-modules).

Tell your desktop environment to run XSecureLock by using a command line such
as one of the following:

```
xsecurelock
env XSECURELOCK_SAVER=saver_xscreensaver xsecurelock
env XSECURELOCK_SAVER=saver_mplayer XSECURELOCK_WANT_FIRST_KEYPRESS=1 xsecurelock
env XSECURELOCK_FONT=`xlsfonts | grep '\<iso8859-1\>' | shuf | head -n 1` xsecurelock
```

Just kidding about the last one :)

# Automatic Locking

To automatically lock the screen after some time of inactivity, use
[xss-lock](https://bitbucket.org/raymonad/xss-lock) as follows:

```
xset s 300 5
xss-lock -n 'xmessage -nearmouse "Locking screen..."' -l -- xsecurelock
```

or on a laptop:

```
xset s 300 5
xss-lock -n /path/to/dim-screen.sh -l -- xsecurelock
```

The option `-l` is critical as it makes sure not to allow machine suspend before
the screen saver is active - otherwise previous screen content may show up for a
short time after wakeup!

# Options

Options to XSecureLock can be passed by environment variables:

* `XSECURELOCK_AUTH`: specifies the desired authentication module.
* `XSECURELOCK_FONT`: X11 font name to use for auth_pam_x11. You can get a list
  of supported font names by running `xlsfonts`.
* `XSECURELOCK_PAM_SERVICE`: pam service name. You should have a file with that
  name in `/etc/pam.d`.
* `XSECURELOCK_SAVER`: specifies the desired screen saver module.
* `XSECURELOCK_WANT_FIRST_KEYPRESS`: If set to 1, the key pressed to stop the
  screen saver and spawn the auth child is sent to the auth child (and thus
  becomes part of the password entry).

# Authentication Modules

The following authentication modules are included:

* `auth_pam_x11`: Authenticates via PAM using keyboard input (X11 based;
  recommended).
* `auth_pamtester`: Authenticates via PAM using keyboard input (pamtester).
* `auth_htpasswd`: Authenticates via a htpasswd style file stored in
  `~/.xsecurelock.pw`. To generate this file, run:
  ```
  ( umask 077; htpasswd -cB ~/.xsecurelock.pw "$USER" )
  ```
  Use this only if you for some reason can't use PAM!

## Writing Your Own Module

The authentication module is a separate executable, whose name must start with
`auth_` and be installed together with the included `auth_` modules (default
location: `/usr/local/libexec/xsecurelock/helpers`).

* Input: it may receive keystroke input from standard input in a
  locale-dependent multibyte encoding (usually UTF-8). Use the `mb*` C functions
  to act on these.
* Output: it may draw on or create windows below `$XSCREENSAVER_WINDOW`.
* Exit status: if authentication was successful, it must return with status
  zero. If it returns with any other status (including e.g. a segfault),
  XSecureLock assumes failed authentication.

# Screen Saver Modules

The following screen saver modules are included:

* `saver_blank`: Simply blanks the screen.
* `saver_mplayer` and `saver_mpv`: Plays a video using mplayer or mpv,
  respectively. The video to play is selected at random among all files in
  `~/Videos`.
* `saver_xscreensaver`: Runs an XScreenSaver hack from an existing XScreenSaver
  setup.

## Writing Your Own Module

The screen saver module is a separate executable, whose name must start with
`saver_` and be installed together with the included `auth_` modules (default
location: `/usr/local/libexec/xsecurelock/helpers`).

* Input: none.
* Output: it may draw on or create windows below `$XSCREENSAVER_WINDOW`.
* Exit condition: the saver child will receive SIGTERM when the user wishes to
  unlock the screen. It should exit promptly.

# Security Design

In order to achieve maximum possible security against screen lock bypass
exploits, the following measures are taken:

* Both authentication and screen saving are done using separate processes.
  Therefore a crash of these processes will not unlock the screen, which means
  that these processes are allowed to do "possibly dangerous" things.
* The main process is kept minimal and only uses C, POSIX and X11 APIs. This
  limits the possible influence from bugs in external libraries, and allows for
  easy auditing.
* The main process regularly refreshes the screen grabs in case they get lost
  for whatever reason.
* The main process regularly brings its window to the front, to avoid leaking
  information from notification messages that are OverrideRedirect.
* The main process resizes its window to the size of the root window, should the
  root window size change, to avoid leaking information by attaching a
  secondary display.
* The main processes uses only a single buffer - to hold a single keystroke.
  Therefore it is impossible to exploit a buffer overrun in the main process by
  e.g. an overlong password entry.
* The only exit conditions of the program is the Authentication Module returning
  with exit status zero, on which xsecurelock itself will return with status
  zero; therefore especially paranoid users might want to run it as
  `sh -c "xsecurelock ... || kill -9 -1"` :)

# License

The code is released unser the Apache 2.0 license. See the LICENSE file for
more details.

This project is not an official Google project. It is not supported by Google
and Google specifically disclaims all warranties as to its quality,
merchantability, or fitness for a particular purpose.
