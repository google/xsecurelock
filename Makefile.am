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

AM_CFLAGS = -Wall -Wextra

bin_PROGRAMS = xsecurelock

xsecurelock_SOURCES = \
	auth_child.c auth_child.h \
	saver_child.c saver_child.h \
	main.c
xsecurelock_CPPFLAGS = \
	-DHELPER_PATH=\"$(pkglibexecdir)\" \
	-DDOCS_PATH=\"$(docdir)\"

if HAVE_SCRNSAVER
xsecurelock_CPPFLAGS += -DHAVE_SCRNSAVER
endif
if HAVE_XF86MISC
xsecurelock_CPPFLAGS += -DHAVE_XF86MISC
endif

helpersdir = $(pkglibexecdir)
helpers_SCRIPTS = \
	helpers/auth_pamtester \
	helpers/saver_blank \
	helpers/saver_mplayer \
	helpers/saver_mpv \
	helpers/saver_xscreensaver

if HAVE_PAM
helpers_PROGRAMS = \
	auth_pam_x11
auth_pam_x11_SOURCES = \
	helpers/auth_pam_x11.c
endif

docsdir = $(docdir)
docs_DATA = \
	CONTRIBUTING \
	COPYING \
	LICENSE-APACHE-2.0 \
	LICENSE-XLIB \
	README.md

EXTRA_DIST = \
	autogen.sh \
	helpers/auth_pamtester \
	helpers/saver_blank \
	helpers/saver_xscreensaver \
	COPYING \
	LICENSE-APACHE-2.0 \
	LICENSE-XLIB
