# About XSecureLock

XSecureLock is an X11 screen lock utility designed with the primary goal of
security.

Security is achieved using a modular design to avoid the usual pitfalls of
screen locking utility design. Details are available in the
[Security Design] (#security-design) section.

# Requirements

The following packages need to be installed; their names depend on your Linux
distribution of choice, but will be similar:

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
* imagemagick (for the `auth_pamtester` module)
* pamtester (for the `auth_pamtester` module)
* x11-xserver-utils (for the `saver_blank` module)
* xscreensaver (for the `saver_xscreensaver` module)

# Installation

```
git clone URL_GOES_HERE
cd xsecurelock
sh autogen.sh
./configure
make
make install
```

# Setup

Pick one of the [authentication modules] (#authentication-modules) and one of
the [screen saver modules] (#screen-saver-modules).

Tell your desktop environment to run
"xsecurelock _authentication_module_ _screen_saver_module_", for example:

```
xsecurelock auth_pam_x11 saver_blank
```

# Authentication Modules

The following authentication modules are included:

* `auth_pam_x11`: Authenticates via PAM using keyboard input (X11 based;
  recommended).
* `auth_pamtester`: Authenticates via PAM using keyboard input (pamtester).

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
  limits the possible influence from bugs in external libraries.
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
* The only exit condition of the program is the Authentication Module returning
  with exit status zero.

# License

The code is released unser the Apache 2.0 license. See the LICENSE file for
more details.

This project is not an official Google project. It is not supported by Google
and Google specifically disclaims all warranties as to its quality,
merchantability, or fitness for a particular purpose.
