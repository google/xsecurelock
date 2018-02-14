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
	env_settings.c env_settings.h \
	saver_child.c saver_child.h \
	mlock_page.h \
	main.c \
	xscreensaver_api.c xscreensaver_api.h
xsecurelock_CPPFLAGS = \
	-DHELPER_PATH=\"$(pkglibexecdir)\" \
	-DDOCS_PATH=\"$(docdir)\" \
	-DAUTH_EXECUTABLE=\"@auth_executable@\" \
	-DGLOBAL_SAVER_EXECUTABLE=\"@global_saver_executable@\" \
	-DSAVER_EXECUTABLE=\"@saver_executable@\"

if HAVE_SCRNSAVER
xsecurelock_CPPFLAGS += -DHAVE_SCRNSAVER
endif
if HAVE_COMPOSITE
xsecurelock_CPPFLAGS += -DHAVE_COMPOSITE
endif
if HAVE_XF86MISC
xsecurelock_CPPFLAGS += -DHAVE_XF86MISC
endif

helpersdir = $(pkglibexecdir)
helpers_SCRIPTS = \
	helpers/auth_htpasswd \
	helpers/auth_pamtester \
	helpers/saver_blank \
	helpers/saver_mplayer \
	helpers/saver_mpv \
	helpers/saver_xscreensaver

helpers_PROGRAMS = \
	saver_multiplex
saver_multiplex_SOURCES = \
	helpers/saver_multiplex.c \
	env_settings.c env_settings.h \
	helpers/monitors.c helpers/monitors.h \
	saver_child.c saver_child.h \
	xscreensaver_api.c xscreensaver_api.h
saver_multiplex_CPPFLAGS = \
	-DSAVER_EXECUTABLE=\"@saver_executable@\"
if HAVE_XRANDR
saver_multiplex_CPPFLAGS += -DHAVE_XRANDR
endif

if HAVE_PAM
helpers_PROGRAMS += \
	auth_pam_x11
auth_pam_x11_SOURCES = \
	helpers/auth_pam_x11.c \
	env_settings.c env_settings.h \
	helpers/monitors.c helpers/monitors.h \
	mlock_page.h \
	xscreensaver_api.c xscreensaver_api.h
auth_pam_x11_CPPFLAGS = \
	-DPAM_SERVICE_NAME=\"@pam_service_name@\"
if PAM_CHECK_ACCOUNT_TYPE
auth_pam_x11_CPPFLAGS += \
	-DPAM_CHECK_ACCOUNT_TYPE
endif
if HAVE_XKB
auth_pam_x11_CPPFLAGS += -DHAVE_XKB
endif
if HAVE_XRANDR
auth_pam_x11_CPPFLAGS += -DHAVE_XRANDR
endif
endif

docsdir = $(docdir)
docs_DATA = \
	CONTRIBUTING \
	LICENSE \
	README.md

EXTRA_DIST = \
	CONTRIBUTING \
	LICENSE \
	README.md \
	helpers/auth_htpasswd \
	helpers/auth_pamtester \
	helpers/saver_blank \
	helpers/saver_mplayer \
	helpers/saver_mpv \
	helpers/saver_xscreensaver \
	Doxyfile \
	autogen.sh

if HAVE_DOXYGEN
doxyfile.stamp:
	$(DOXYGEN) Doxyfile
	echo Timestamp > doxyfile.stamp

CLEANFILES = doxyfile.stamp

all-local: doxyfile.stamp

clean-local:
	$(RM) -r $(top_srcdir)/doxy
endif
