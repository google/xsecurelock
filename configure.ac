# Copyright 2014 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

AC_INIT([xsecurelock],[0.5],[rpolzer@google.com])
AM_INIT_AUTOMAKE([-Wall subdir-objects foreign])
AC_PROG_CC

AC_CHECK_HEADER(X11/Xlib.h,
                [], [AC_ERROR([X includes not found.])])
AC_CHECK_LIB(X11, XOpenDisplay,
             [], [AC_ERROR([X library not found.])])
AC_CHECK_LIB(rt, timer_settime,
             [], [AC_ERROR([Timer library not found.])])

# RP_SEARCH_LIBS(sym, lib, macro, flag, default, description)
AC_DEFUN([RP_SEARCH_LIBS], [
          AC_ARG_WITH([$4],
                      [AS_HELP_STRING([--with-$4],
                                      [$6 @<:@default=$5@:>@])],
                                      [with_$4=$withval],
                                      [with_$4=check])
          AS_IF([test "x$with_$4" = xno],
                [have_$4=false],
                [AC_SEARCH_LIBS([$1], [$2],
                                [have_$4=true],
                                [AS_IF([test "x$with_$4" = xcheck],
                                       [have_$4=false],
                                       [AC_MSG_ERROR([--with-$4 was enabled, but test for $4 failed])])])])
          AM_CONDITIONAL([$3], [test x$have_$4 = xtrue])])

# The X11 Screen Saver extension is used to turn off the screen saver when X11
# handles screen blanking (e.g. via timeout) anyway. Saves CPU power.
RP_SEARCH_LIBS(XScreenSaverQueryExtension, Xss,
               [HAVE_SCRNSAVER], [xss], [check],
               [Use the X11 Screen Saver extension to save power])

# The Composite extension needs an explicit opt-out because not having it can
# cause desktop notifications to appear on top of the screen saver (privacy
# risk).
RP_SEARCH_LIBS(XCompositeQueryExtension, Xcomposite,
               [HAVE_COMPOSITE], [xcomposite], [yes],
               [Use the X11 Composite extension to cover desktop notifications])

# This extension doesn't really exist anymore, but served to counteract the
# (also no longer existing) AllowClosedownGrabs and similar X server settings.
RP_SEARCH_LIBS(XF86MiscSetGrabKeysState, Xxf86misc,
               [HAVE_XF86MISC], [xf86misc], [check],
               [Use the XFree86-Misc extension to disable ungrab keys])

# PAM-less operation is... unfortunate. Let's have users explicitly disable this
# if they need to use other auth modules, as most likely they should rather
# install PAM development libraries instead.
RP_SEARCH_LIBS(pam_authenticate, pam,
               [HAVE_PAM], [pam], [yes],
               [Enable support for the PAM library to authenticate the user])

# Querying the keyboard LEDs (and layout name) makes auth_pam_x11 more user
# friendly.
RP_SEARCH_LIBS(XkbGetIndicatorState, X11,
               [HAVE_XKB], [xkb], [check],
               [Use the X Keyboard extension to show the keyboard LEDs])

AC_SEARCH_LIBS(XRRGetMonitors, Xrandr,
               [have_xrandr15=true], [have_xrandr15=false])
AM_CONDITIONAL([HAVE_XRANDR15], [test x$have_xrandr15 = xtrue])

AC_SEARCH_LIBS(XRRGetScreenResources, Xrandr,
               [have_xrandr12=true], [have_xrandr12=false])
AM_CONDITIONAL([HAVE_XRANDR12], [test x$have_xrandr12 = xtrue])

pam_service_name=
enable_pam_check_account_type=no

AS_IF([test x$have_pam = xtrue], [
  AC_ARG_WITH([pam_service_name],
              AC_HELP_STRING([--with-pam-service-name],
                             [The name of the PAM service that xsecurelock's
                              auth helpers will authenticate as by default.
                              This flag is mandatory. Be sure to verify that
                              /etc/pam.d/SERVICENAME actually exists, as failure
                              of doing so may prevent unlocking the screen.]),
              [pam_service_name=$withval],
              [AC_MSG_ERROR([--with-pam-service-name is mandatory.])])
  AS_IF([test -f "/etc/pam.d/$pam_service_name"], [],
        [AC_MSG_WARN([/etc/pam.d/$pam_service_name doesn't exist. Sure?])])

  AC_ARG_ENABLE([pam_check_account_type],
                AC_HELP_STRING([--enable-pam-check-account-type],
                               [Enable checking of PAM accounting result. Only
                                enable this if accounting is properly configured
                                on your system; especially keep this disabled if
                                --with-pam-service-name is set to an
                                authentication-only service name like
                                common-auth.]),
                [enable_pam_check_account_type=$enableval],
                [enable_pam_check_account_type=no])
])

AC_ARG_WITH([default_auth_module],
            AC_HELP_STRING([--with-default-auth-module],
                           [The default authentication module to use when none
                            was specified. Can be overridden via the
                            XSECURELOCK_AUTH environment variable or a command
                            line argument. By default this is autodetected to be
                            one of the supported authentication modules that
                            require no setup.]),
                            [auth_executable=$withval],
                            [
                             auth_executable=
                             pamtester=
                             AC_CHECK_PROGS([pamtester], [pamtester])
                             AS_IF([test -n "$pamtester"],
                                   [auth_executable=auth_pamtester])
                             AS_IF([test x$have_pam = xtrue],
                                   [auth_executable=auth_pam_x11])
                             AS_IF([test -z "$auth_executable"],
                                   [AC_MSG_ERROR([No auth executable found.])])
                            ])

AC_MSG_NOTICE([Configured default auth module: $auth_executable])

AC_ARG_WITH([default_global_saver_module],
            AC_HELP_STRING([--with-default-global-saver-module],
                           [The default global saver module to use when none was
                            pecified. Can be overridden via the
                            XSECURELOCK_GLOBAL_SAVER environment variable.]),
                            [global_saver_executable=$withval],
                            [global_saver_executable=saver_multiplex])

AC_MSG_NOTICE([Configured default global saver module: ]dnl
[$global_saver_executable])

AC_ARG_WITH([default_saver_module],
            AC_HELP_STRING([--with-default-saver-module],
                           [The default per-screen saver module to use when none
                            was pecified. Can be overridden via the
                            XSECURELOCK_SAVER environment variable.]),
                            [saver_executable=$withval],
                            [saver_executable=saver_blank])

AC_MSG_NOTICE([Configured default saver module: $saver_executable])

AC_SUBST(pam_service_name)
AC_SUBST(auth_executable)
AC_SUBST(global_saver_executable)
AC_SUBST(saver_executable)
AM_CONDITIONAL([PAM_CHECK_ACCOUNT_TYPE],
               [test x$enable_pam_check_account_type = xyes])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([helpers/auth_pamtester], [chmod +x helpers/auth_pamtester])

# Generate documentation.
AC_CHECK_PROGS([DOXYGEN], [doxygen], [])
AS_IF([test -z "$DOXYGEN"],
      [AC_MSG_WARN([Doxygen not found. Who needs documentation anyway.])])
AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])
AC_CONFIG_FILES([Doxyfile])

# End of documentation.

AC_OUTPUT
