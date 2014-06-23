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

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
