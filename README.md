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

*   apache2-utils (for the `auth_htpasswd` module)
*   autotools-dev
*   autoconf (for Ubuntu 18.04 and newer)
*   binutils
*   gcc
*   libc6-dev
*   libpam0g-dev (for Ubuntu 18.04 and newer)
*   libpam-dev (for the `authproto_pam` module)
*   libx11-dev
*   libxcomposite-dev
*   libxext-dev
*   libxfixes-dev
*   libxft-dev
*   libxmuu-dev
*   libxrandr-dev
*   libxss-dev
*   make
*   mplayer (for the `saver_mplayer` module)
*   mpv (for the `saver_mpv` module)
*   pamtester (for the `authproto_pamtester` module)
*   pkg-config
*   x11proto-core-dev
*   xscreensaver (for the `saver_xscreensaver` module)

# Installation

NOTE: In these instructions, please replace SERVICE-NAME by the name of an
appropriate and existing file in `/etc/pam.d`. If xscreensaver is installed,
`xscreensaver` should always be a good choice; otherwise, on Debian and Ubuntu,
`common-auth` would work. This will be used as default and can be overridden
with [`XSECURELOCK_PAM_SERVICE`](#options).

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
sudo make install
```

## Special notes for FreeBSD and NetBSD

First of all, on BSD systems, `/usr/local` is owned by the ports system, so
unless you are creating a port, it is recommended to install to a separate
location by specifying something like `--prefix=/opt/xsecurelock` in the
`./configure` call. You can then run XSecureLock as
`/opt/xsecurelock/bin/xsecurelock`.

Also, in order to authenticate with PAM on FreeBSD and NetBSD, you must be root
so you can read the shadow password database. The `authproto_pam` binary can be
made to acquire these required privileges like this:

```
chmod +s /opt/xsecurelock/libexec/xsecurelock/authproto_pam
```

## Special notes for OpenBSD

First of all, on BSD systems, `/usr/local` is owned by the ports system, so
unless you are creating a port, it is recommended to install to a separate
location by specifying something like `--prefix=/opt/xsecurelock` in the
`./configure` call. You can then run XSecureLock as
`/opt/xsecurelock/bin/xsecurelock`.

Also, in order to authenticate with PAM on OpenBSD, you must be in the `auth`
group so you can run a setuid helper called `login_passwd` that can read the
shadow password database. The `authproto_pam` binary can be made to acquire
these required privileges like this:

```
chgrp auth /opt/xsecurelock/libexec/xsecurelock/authproto_pam
chmod g+s /opt/xsecurelock/libexec/xsecurelock/authproto_pam
```

Note that this adds substantially less attack surface than adding your own user
to the `auth` group, as the `login_passwd` binary can try out passwords of any
user, while `authproto_pam` is restricted to trying your own user.

# Setup

Pick one of the [authentication modules](#authentication-modules) and one of the
[screen saver modules](#screen-saver-modules).

Tell your desktop environment to run XSecureLock by using a command line such as
one of the following:

```
xsecurelock
env XSECURELOCK_SAVER=saver_xscreensaver xsecurelock
env XSECURELOCK_SAVER=saver_mplayer XSECURELOCK_DISCARD_FIRST_KEYPRESS=0 xsecurelock
env XSECURELOCK_FONT=`xlsfonts | grep '\<iso10646-1\>' | shuf | head -n 1` xsecurelock
```

Just kidding about the last one :)

IMPORTANT: Make sure your desktop environment does not launch any other locker,
be it via autostart file or its own configuration, as multiple screen lockers
may interfere with each other. You have been warned!

# Automatic Locking

To automatically lock the screen after some time of inactivity, use
[xss-lock](https://bitbucket.org/raymonad/xss-lock) as follows:

```
xset s 300 5
xss-lock -n /usr/lib/xsecurelock/dimmer -l -- xsecurelock
```

The option `-l` is critical as it makes sure not to allow machine suspend before
the screen saver is active - otherwise previous screen content may show up for a
short time after wakeup!

NOTE: When using `xss-lock`, it's recommended to not launch `xsecurelock`
directly for manual locking, but to manually lock using `xset s activate`. This
ensures that `xss-lock` knows about the locking state and won't try again, which
would spam the X11 error log.

WARNING: Never rely on automatic locking for security, for the following
reasons:

-   An attacker can, of course, use your computer after you leave it alone and
    before it locks or you return.

-   Automatic locking is unreliable by design - for example, it could simply be
    misconfigured, or a pointer grab (due to open context menu) could prevent
    the screen lock from ever activating. Media players also often suspend
    screen saver activation for usability reasons.

Automatic locking should merely be seen as a fallback for the case of the user
forgetting to lock explicitly, and not as a security feature. If you really want
to use this as a security feature, make sure to kill the session whenever
attempts to lock fail (in which case `xsecurelock` will return a non-zero exit
status).

## Alternatives

### xautolock

`xautolock` can be used instead of `xss-lock` as long as you do not care for
suspend events (like on laptops):

```
xautolock -time 10 -notify 5 -notifier '/usr/lib/xsecurelock/until_nonidle /usr/lib/xsecurelock/dimmer' -locker xsecurelock
```

### Possible other tools

Ideally, an environment integrating `xsecurelock` should provide the following
facilities:

1.  Wait for one of the following events:
    1.  When idle for a sufficient amount of time:
        1.  Run `dimmer`.
        2.  When no longer idle while dimmed, kill `dimmer` and go back to the
            start.
        3.  When `dimmer` exits, run `xsecurelock` and wait for it.
    2.  When locking was requested, run `xsecurelock` and wait for it.
    3.  When suspending, run `xsecurelock` while passing along
        `XSS_SLEEP_LOCK_FD` and wait for it.
2.  Repeat.

This is, of course precisely what `xss-lock` does, and - apart from the suspend
handling - what `xautolock` does.

As an alternative, we also support this way of integrating:

1.  Wait for one of the following events:
    1.  When idle for a sufficient amount of time:
        1.  Run `until_nonidle dimmer || exec xsecurelock` and wait for it.
        2.  Reset your idle timer (optional when your idle timer is either the
            X11 Screen Saver extension's idle timer or the X Synchronization
            extension's `"IDLETIME"` timer, as this command can never exit
            without those being reset).
    2.  When locking was requested, run `xsecurelock` and wait for it.
    3.  When suspending, run `xsecurelock` while passing along
        `XSS_SLEEP_LOCK_FD` and wait for it.
2.  Repeat.

NOTE: When using `until_nonidle` with other dimming tools than the included
`dimmer`, please set `XSECURELOCK_DIM_TIME_MS` and `XSECURELOCK_WAIT_TIME_MS` to
match the time your dimming tool takes for dimming, and how long you want to
wait in dimmed state before locking.

# Options

Options to XSecureLock can be passed by environment variables:

<!-- ENV VARIABLES START -->

*   `XSECURELOCK_AUTH`: specifies the desired authentication module (the part
    that displays the authentication prompt).
*   `XSECURELOCK_AUTHPROTO`: specifies the desired authentication protocol
    module (the part that talks to the system).
*   `XSECURELOCK_AUTH_BACKGROUND_COLOR`: specifies the X11 color (see manpage of
    XParseColor) for the background of the auth dialog.
*   `XSECURELOCK_AUTH_CURSOR_BLINK`: if set, the cursor will blink in the auth
    dialog. Enabled by default, can be set to 0 to disable.
*   `XSECURELOCK_AUTH_SOUNDS`: specifies whether to play sounds during
    authentication to indicate status. Sounds are defined as follows:
    *   High-pitch ascending: prompt for user input.
    *   High-pitch constant: an info message was displayed.
    *   Low-pitch descending: an error message was displayed.
    *   Medium-pitch ascending: authentication successful.
*   `XSECURELOCK_AUTH_FOREGROUND_COLOR`: specifies the X11 color (see manpage of
    XParseColor) for the foreground text of the auth dialog.
*   `XSECURELOCK_AUTH_TIMEOUT`: specifies the time (in seconds) to wait for
    response to a prompt by `auth_x11` before giving up and reverting to
    the screen saver.
*   `XSECURELOCK_AUTH_WARNING_COLOR`: specifies the X11 color (see manpage of
    XParseColor) for the warning text of the auth dialog.
*   `XSECURELOCK_BLANK_TIMEOUT`: specifies the time (in seconds) before telling
    X11 to fully blank the screen; a negative value disables X11 blanking. The
    time is measured since the closing of the auth window or xsecurelock
    startup. Setting this to 0 is rather nonsensical, as key-release events
    (e.g. from the keystroke to launch xsecurelock or from pressing escape to
    close the auth dialog) always wake up the screen.
*   `XSECURELOCK_BLANK_DPMS_STATE`: specifies which DPMS state to put the screen
    in when blanking (one of standby, suspend, off and on, where "on" means to
    not invoke DPMS at all).
*   `XSECURELOCK_BURNIN_MITIGATION`: specifies the number of pixels the prompt
    of `auth_x11` may be moved at startup to mitigate possible burn-in
    effects due to the auth dialog being displayed all the time (e.g. when
    spurious mouse events wake up the screen all the time).
*   `XSECURELOCK_BURNIN_MITIGATION_DYNAMIC`: if set to a non-zero value,
    `auth_x11` will move the prompt while it is being displayed, but stay
    within the bounds of `XSECURELOCK_BURNIN_MITIGATION`. The value of this
    variable is the maximum allowed shift per screen refresh. This mitigates
    short-term burn-in effects but is probably annoying to most users, and thus
    disabled by default.
*   `XSECURELOCK_COMPOSITE_OBSCURER`: create a second full-screen window to
    obscure window content in case a running compositor unmaps its own window.
    Helps with some instances of bad compositor behavior (such as compositor
    crashes/restarts, but also compton has been caught at drawing notification
    icons above the screen locker when not using the GLX backend), should
    prevent compositors from unredirecting as it's 1 pixel smaller than the
    screen from every side, and should otherwise be harmless, so it's enabled
    by default.
*   `XSECURELOCK_DATETIME_FORMAT`: the date format to show. Defaults to the
    locale settings. (see `man date` for possible formats)
*   `XSECURELOCK_DEBUG_ALLOW_LOCKING_IF_INEFFECTIVE`: Normally we don't allow
    locking sessions that are likely not any useful to lock, such as the X11
    part of a Wayland session (one could still use Wayland applicatione when
    locked) or VNC sessions (as it'd only lock the server side session while
    users will likely think they locked the client, allowing for an easy
    escape). These checks can be bypassed by setting this variable to 1. Not
    recommended other than for debugging XSecureLock itself via such
    connections.
*   `XSECURELOCK_DEBUG_WINDOW_INFO`: When complaining about another window
    misbehaving, print not just the window ID but also some info about it. Uses
    the `xwininfo` and `xprop` tools.
*   `XSECURELOCK_DIM_ALPHA`: Linear-space opacity to fade the screen to.
*   `XSECURELOCK_DIM_COLOR`: X11 color to fade the screen to.
*   `XSECURELOCK_DIM_FPS`: Target framerate to attain during the dimming effect
    of `dimmer`. Ideally matches the display refresh rate.
*   `XSECURELOCK_DIM_MAX_FILL_SIZE`: Maximum size (in width or height) to fill
    at once using an XFillRectangle call. Low values may cause performance loss
    or noticeable tearing during dimming; high values may cause crashes or hangs
    with some graphics drivers or a temporarily unresponsive X server.
*   `XSECURELOCK_DIM_OVERRIDE_COMPOSITOR_DETECTION`: When set to 1, always try
    to use transparency for dimming; when set to 0, always use a dither
    pattern. Default is to autodetect whether transparency will likely work.
*   `XSECURELOCK_DIM_TIME_MS`: Milliseconds to dim for when above xss-lock
    command line with `dimmer` is used; also used by `wait_nonidle` to know when
    to assume dimming and waiting has finished and exit.
*   `XSECURELOCK_DISCARD_FIRST_KEYPRESS`: If set to 0, the key pressed to stop
    the screen saver and spawn the auth child is sent to the auth child (and
    thus becomes part of the password entry). By default we always discard the
    key press that started the authentication flow, to prevent users from
    getting used to type their password on a blank screen (which could be just
    powered off and have a chat client behind or similar).
*   `XSECURELOCK_FONT`: X11 or FontConfig font name to use for `auth_x11`.
    You can get a list of supported font names by running `xlsfonts` and
    `fc-list`.
*   `XSECURELOCK_FORCE_GRAB`: When grabbing fails, try stealing the grab from
    other windows (a value of `2` steals from all descendants of the root
    window, while a value of `1` only steals from client windows). This works
    only sometimes and is incompatible with many window managers, so use with
    care. See the "Forcing Grabs" section below for details.
*   `XSECURELOCK_GLOBAL_SAVER`: specifies the desired global screen saver module
    (by default this is a multiplexer that runs `XSECURELOCK_SAVER` on each
    screen).
*   `XSECURELOCK_IDLE_TIMERS`: comma-separated list of idle time counters used
    by `until_nonidle`. Typical values are either empty (relies on the X Screen
    Saver extension instead), "IDLETIME" and "DEVICEIDLETIME <n>" where n is an
    XInput device index (run `xinput` to see them). If multiple time counters
    are specified, the idle time is the minimum of them all. All listed timers
    must have the same unit.
*   `XSECURELOCK_IMAGE_DURATION_SECONDS`: how long to show each still image
    played by `saver_mpv`. Defaults to 1.
*   `XSECURELOCK_KEY_%s_COMMAND` where `%s` is the name of an X11 keysym (find
    using `xev`): a shell command to execute when the specified key is pressed.
    Useful e.g. for media player control. Beware: be cautious about what you
    run with this, as it may yield attackers control over your computer.
*   `XSECURELOCK_LIST_VIDEOS_COMMAND`: shell command to list all video files to
    potentially play by `saver_mpv` or `saver_mplayer`. Defaults to
    `find ~/Videos -type f`.
*   `XSECURELOCK_NO_COMPOSITE`: disables covering the composite overlay window.
    This switches to a more traditional way of locking, but may allow desktop
    notifications to be visible on top of the screen lock. Not recommended.
*   `XSECURELOCK_NO_PAM_RHOST`: do not set `PAM_RHOST` to `localhost`, despite
    [recommendation](http://www.linux-pam.org/Linux-PAM-html/adg-security-user-identity.html)
    to do so by the Linux-PAM Application Developers' Guide. This may work
    around bugs in third-party PAM authentication modules. If this solves a
    problem for you, please report a bug against said PAM module.
*   `XSECURELOCK_NO_XRANDR`: disables multi monitor support using XRandR.
*   `XSECURELOCK_NO_XRANDR15`: disables multi monitor support using XRandR 1.5
    and fall back to XRandR 1.2. Not recommended.
*   `XSECURELOCK_PAM_SERVICE`: pam service name. You should have a file with
    that name in `/etc/pam.d`.
*   `XSECURELOCK_PASSWORD_PROMPT`: Choose password prompt mode:
    *   `asterisks`: shows asterisks, like classic password prompts. This is
        the least secure option because password length is visible.

            ***_
            *******_

    *   `cursor`: shows a cursor that jumps around on each key press. This is
        the default.

            ________|_______________________
            ___________________|____________

    *   `disco`: shows dancers, which dance around on each key press. Requires a
        font that can handle Unicode line drawing characters, and FontConfig.

            ┏(･o･)┛ ♪ ┗(･o･)┓ ♪ ┏(･o･)┛ ♪ ┗(･o･)┓ ♪ ┏(･o･)┛
            ┗(･o･)┓ ♪ ┏(･o･)┛ ♪ ┏(･o･)┛ ♪ ┏(･o･)┛ ♪ ┏(･o･)┛

    *   `emoji`: shows an emoji, changing which one on each key press. Requires
        a font that can handle emoji, and FontConfig.

            👍
            🎶
            💕

    *   `emoticon`: shows an ascii emoticon, changing which one on each key
        press.

            :-O
            d-X
            X-\

    *   `hidden`: completely hides the password, and there's no feedback for
        keypresses. This would almost be most secure - however as it gives no
        feedback to input whatsoever, you may not be able to notice accidentally
        typing to another computer and sending your password to some chatroom.

        ```
        ```

    *   `kaomoji`: shows a kaomoji (Japanese emoticon), changing which one on
        each key press. Requires a Japanese font, and FontConfig.

            (͡°͜ʖ͡°)
            (＾ｕ＾)
            ¯\_(ツ)_/¯

    *   `time`: shows the current time since the epoch on each keystroke. This
        may be the most secure mode, as it gives feedback to keystroke based
        exclusively on public information, and does not carry over any state
        between keystrokes whatsoever - not even some form of randomness.

            1559655410.922329

    *   `time_hex`: same as `time`, but in microseconds and hexadecimal.
        "Because we can".

            0x58a7f92bd7359

*   `XSECURELOCK_SAVER`: specifies the desired screen saver module.
*   `XSECURELOCK_SAVER_RESET_ON_AUTH_CLOSE`: specifies whether to reset the
    saver module when the auth dialog closes. Resetting is done by sending
    `SIGUSR1` to the saver, which may either just terminate, or handle this
    specifically to do a cheaper reset.
*   `XSECURELOCK_SHOW_DATETIME`: whether to show local date and time on the
    login. Disabled by default.
*   `XSECURELOCK_SHOW_HOSTNAME`: whether to show the hostname on the login
    screen of `auth_x11`. Possible values are 0 for not showing the
    hostname, 1 for showing the short form, and 2 for showing the long form.
*   `XSECURELOCK_SHOW_USERNAME`: whether to show the username on the login
    screen of `auth_x11`.
*   `XSECURELOCK_SINGLE_AUTH_WINDOW`: whether to show only a single auth window
    from `auth_x11`, as opposed to one per screen.
*   `XSECURELOCK_SWITCH_USER_COMMAND`: shell command to execute when `Win-O` or
    `Ctrl-Alt-O` are pressed (think "_other_ user"). Typical values could be
    `lxdm -c USER_SWITCH`, `dm-tool switch-to-greeter`, `gdmflexiserver` or
    `kdmctl reserve`, depending on your desktop environment.
*   `XSECURELOCK_VIDEOS_FLAGS`: flags to append when invoking mpv/mplayer with
    `saver_mpv` or `saver_mplayer`. Defaults to empty.
*   `XSECURELOCK_WAIT_TIME_MS`: Milliseconds to wait after dimming (and before
    locking) when above xss-lock command line is used. Should be at least as
    large as the period time set using "xset s". Also used by `wait_nonidle` to
    know when to assume dimming and waiting has finished and exit.
*   `XSECURELOCK_XSCREENSAVER_PATH`: Location where XScreenSaver hacks are
    installed for use by `saver_xscreensaver`.

<!-- ENV VARIABLES END -->

Additionally, command line arguments following a "--" argument will be executed
via `execvp` once locking is successful; this can be used to notify a calling
process of successful locking.

# Authentication Modules

The following authentication modules are included:

*   `auth_x11`: Authenticates via an authproto module using keyboard input (X11
    based; recommended).

## Writing Your Own Module

The authentication module is a separate executable, whose name must start with
`auth_` and be installed together with the included `auth_` modules (default
location: `/usr/local/libexec/xsecurelock/helpers`).

*   Input: it may receive keystroke input from standard input in a
    locale-dependent multibyte encoding (usually UTF-8). Use the `mb*` C
    functions to act on these.
*   Output: it may draw on or create windows below `$XSCREENSAVER_WINDOW`.
*   Exit status: if authentication was successful, it must return with status
    zero. If it returns with any other status (including e.g. a segfault),
    XSecureLock assumes failed authentication.
*   It is recommended that it shall spawn the configured authentication
    protocol module and let it do the actual authentication; that way the
    authentication module can focus on the user interface alone.

# Authentication Protocol Modules

The following authentication protocol ("authproto") modules are included:

*   `authproto_htpasswd`: Authenticates via a htpasswd style file stored in
    `~/.xsecurelock.pw`. To generate this file, run: `( umask 077; htpasswd -cB
    ~/.xsecurelock.pw "$USER" )` Use this only if you for some reason can't use
    PAM!
*   `authproto_pam`: Authenticates via PAM. Use this.
*   `authproto_pamtester`: Authenticates via PAM using pamtester. Shouldn't
    be required unless you can't compile `authproto_pam`. Only supports simple
    password based conversations.

## Writing Your Own Module

The authentication protocol module is a separate executable, whose name must
start with `authproto_` and be installed together with the included
`authproto_` modules (default location:
`/usr/local/libexec/xsecurelock/helpers`).

*   Input: in response to some output messages, it may receive authproto
    messages. See helpers/authproto.h for details.
*   Output: it should output authproto messages; see helpers/authproto.h for
    details.
*   Exit status: if authentication was successful, it must return with status
    zero. If it returns with any other status (including e.g. a segfault),
    XSecureLock assumes failed authentication.

# Screen Saver Modules

The following screen saver modules are included:

*   `saver_blank`: Simply blanks the screen.
*   `saver_mplayer` and `saver_mpv`: Plays a video using mplayer or mpv,
    respectively. The video to play is selected at random among all files in
    `~/Videos`.
*   `saver_multiplex`: Watches the display configuration and runs another screen
    saver module once on each screen; used internally.
*   `saver_xscreensaver`: Runs an XScreenSaver hack from an existing
    XScreenSaver setup. NOTE: some screen savers included by this may display
    arbitrary pictures from your home directory; if you care about this, either
    run `xscreensaver-demo` and disable screen savers that may do this, or stay
    away from this one!

## Writing Your Own Module

The screen saver module is a separate executable, whose name must start with
`saver_` and be installed together with the included `auth_` modules (default
location: `/usr/local/libexec/xsecurelock/helpers`).

*   Input: none.
*   Output: it may draw on or create windows below `$XSCREENSAVER_WINDOW`.
*   Exit condition: the saver child will receive SIGTERM when the user wishes to
    unlock the screen. It should exit promptly.
*   Reset condition: the saver child will receive SIGUSR1 when the auth dialog
    is closed and `XSECURELOCK_SAVER_RESET_ON_AUTH_CLOSE`.

# Security Design

In order to achieve maximum possible security against screen lock bypass
exploits, the following measures are taken:

*   Authentication dialog, authentication checking and screen saving are done
    using separate processes. Therefore a crash of these processes will not
    unlock the screen, which means that these processes are allowed to do
    "possibly dangerous" things.
*   This also means that on operating systems where authentication checking
    requires special privileges (such as FreeBSD), only that module can be set
    to run at elevated privileges, unlike most other screen lockers which in
    this scenario also run graphical user interface code as root.
*   The main process is kept minimal and only uses C, POSIX and X11 APIs. This
    limits the possible influence from bugs in external libraries, and allows
    for easy auditing.
*   The main process regularly refreshes the screen grabs in case they get lost
    for whatever reason.
*   The main process regularly brings its window to the front, to avoid leaking
    information from notification messages that are OverrideRedirect.
*   The main process resizes its window to the size of the root window, should
    the root window size change, to avoid leaking information by attaching a
    secondary display.
*   The main processes uses only a single buffer - to hold a single keystroke.
    Therefore it is impossible to exploit a buffer overrun in the main process
    by e.g. an overlong password entry.
*   The only exit conditions of the program is the Authentication Module
    returning with exit status zero, on which xsecurelock itself will return
    with status zero; therefore especially security-conscious users might want
    to run it as `sh -c "xsecurelock ... || kill -9 -1"` :)

# Known Security Issues

*   Locking the screen will fail while other applications already have a
    keyboard or pointer grab open (for example while running a fullscreen game,
    or after opening a context menu). This will be noticeable as the screen will
    not turn black and should thus usually not be an issue - however when
    relying on automatic locking via `xss-lock`, this could leave a workstation
    open for days. Above `... || kill -9 -1` workaround would mitigate this
    issue too by simply killing the entire session if locking it fails.
*   As XSecureLock relies on an event notification after a screen configuration
    change, window content may be visible for a short time after attaching a
    monitor. No usual interaction with applications should be possible though.
    On desktop systems where monitors are usually not hotplugged, I'd recommend
    [turning off automatic screen
    reconfiguration](http://tech.draiser.net/2015/07/14/ignoring-hotplug-monitor-events-on-arch-linux/).
*   XSecureLock relies on a keyboard and pointer grab in order to prevent other
    applications from receiving keyboard events (and thus an unauthorized user
    from controlling the machine). However, there are various other ways for
    applications - in particular games - to receive input:
    *   Polling current keyboard status (`XQueryKeymap`).
    *   Polling current mouse position (`XQueryPointer`).
    *   Receiving input out-of-band (`/dev/input`), including other input
        devices than keyboard and mouse, such as gamepads or joysticks.

Most these issues are inherent with X11 and can only really be fixed by
migrating to an alternative such as Wayland; some of the issues (in particular
the gamepad input issue) will probably persist even with Wayland.

## Forcing Grabs

As a workaround to the issue of another window already holding a grab, we offer
an `XSECURELOCK_FORCE_GRAB` option.

This adds a last measure attempt to force grabbing by iterating through all
subwindows of the root window, unmapping them (which closes down their grabs),
then taking the grab and mapping them again.

This has the following known issues:

*   Grabs owned by the root window cannot be closed down this way. However,
    only screen lockers and fullscreen games should be doing that.
*   If the grab was owned by a full screen window (e.g. a game using
    `OverrideRedirect` to gain fullscreen mode), the window will become
    unresponsive, as your actions will be interpreted by another window - which
    you can't see - instead. Alt-Tabbing around may often work around this.
*   If the grab was owned by a context menu, it may become impossible to close
    the menu other than by selecting an item in it.
*   It will also likely confuse window managers:
    *   Probably all window managers will rearrange the windows in response to
        this.
    *   Cinnamon (and probably other GNOME-derived WMs) may become unresponsive
        and needs to be restarted.
        *   As a mitigation we try to hit only client windows - but then we
            lose the ability of closing down window manager owned grabs.
*   Negative side effects as described are still likely to happen in case the
    measure fails.

# Known Compatibility Issues

*   There is an open issue with the NVidia graphics driver in conjunction with
    some compositors. Workarounds include switching to the `nouveau` graphics
    driver, using a compositor that uses the Composite Overlay Window (e.g.
    `compton` with the flags `--backend glx --paint-on-overlay`) or passing
    `XSECURELOCK_NO_COMPOSITE=1` to XSecureLock (which however may make
    notifications appear on top of the screen lock).

*   XSecureLock is incompatible with the compositor built into `metacity` (a
    GNOME component) because it draws on the Compositor Overlay Window with
    `IncludeInferiors` set (i.e. it explicitly requests to draw on top of
    programs like XSecureLock). It likely does this because the same is
    necessary when drawing on top of the root window, which it had done in the
    past but no longer does. Workarounds include disabling its compositor with
    `gsettings set org.gnome.metacity compositing-manager false` or passing
    `XSECURELOCK_NO_COMPOSITE=1` to XSecureLock.

*   Picom doesn't remove windows in the required order causing a window with
    the text "INCOMPATIBLE COMPOSITOR, PLEASE FIX!" to be displayed. To fix this
    you can disable composite obscurer with `XSECURELOCK_COMPOSITE_OBSCURER=0`
    to stop the window from being drawn all together.

*   In general, most compositor issues will become visible in form of a text
    "INCOMPATIBLE COMPOSITOR, PLEASE FIX!" being displayed. A known good
    compositor is `compton --backend glx --paint-on-overlay`. In worst case
    you can turn off our workaround for transparent windows by setting
    `XSECURELOCK_NO_COMPOSITE=1`.

# License

The code is released under the Apache 2.0 license. See the LICENSE file for more
details.

This project is not an official Google project. It is not supported by Google
and Google specifically disclaims all warranties as to its quality,
merchantability, or fitness for a particular purpose.
