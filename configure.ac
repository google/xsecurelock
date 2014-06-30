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

AC_INIT([xsecurelock],[0.1],[rpolzer@google.com])
AM_INIT_AUTOMAKE([-Wall subdir-objects foreign])
AC_PROG_CC

AC_CHECK_HEADER(X11/Xlib.h,
                [], [AC_ERROR([X includes not found.])])
AC_CHECK_LIB(X11, XOpenDisplay,
             [], [AC_ERROR([X library not found.])])

AC_SEARCH_LIBS(XScreenSaverQueryExtension, Xss,
               [have_scrnsaver=true], [have_scrnsaver=false])
AM_CONDITIONAL([HAVE_SCRNSAVER], [test x$have_scrnsaver = xtrue])

AC_SEARCH_LIBS(XF86MiscSetGrabKeysState, Xxf86misc,
               [have_xf86misc=true], [have_xf86misc=false])
AM_CONDITIONAL([HAVE_XF86MISC], [test x$have_xf86misc = xtrue])

AC_SEARCH_LIBS(pam_authenticate, pam,
               [have_pam=true], [have_pam=false])
AM_CONDITIONAL([HAVE_PAM], [test x$have_pam = xtrue])

AC_ARG_WITH([pam_service_name],
            AC_HELP_STRING([--with-pam-service-name],
                           [The name of the PAM service that xsecurelock's auth
                            helpers will authenticate as. This flag is
                            mandatory. Be sure to verify that
                            /etc/pam.d/SERVICENAME actually exists, as failure
                            of doing so may prevent unlocking the screen.]),
            [pam_service_name=$withval],
            [AC_MSG_ERROR([--with-pam-service-name is mandatory. See --help.])])
AC_SUBST(pam_service_name)

AC_ARG_ENABLE([pam_check_account_type],
              AC_HELP_STRING([--enable-pam-check-account-type], [Enable checking
                              of PAM accounting result. Only enable this if
                              accounting is properly configured on your system;
                              especially keep this disabled if
                              --with-pam-service-name is set to an
                              authentication-only service name like
                              common-auth.]),
              [enable_pam_check_account_type=$enableval],
              [enable_pam_check_account_type=no])
AM_CONDITIONAL([PAM_CHECK_ACCOUNT_TYPE],
               [test x$enable_pam_check_account_type = xyes])

AS_IF([test -f "/etc/pam.d/$pam_service_name"], [],
      [AC_MSG_WARN([/etc/pam.d/$pam_service_name doesn't exist. Is that OK?])])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([helpers/auth_pamtester], [chmod +x helpers/auth_pamtester])
AC_OUTPUT
