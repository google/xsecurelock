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

AM_CFLAGS = -Wall -Wextra -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=600

macros = \
	-DHELPER_PATH=\"$(pkglibexecdir)\" \
	-DDOCS_PATH=\"$(docdir)\" \
	-DAUTH_EXECUTABLE=\"@auth_executable@\" \
	-DAUTHPROTO_EXECUTABLE=\"@authproto_executable@\" \
	-DGLOBAL_SAVER_EXECUTABLE=\"@global_saver_executable@\" \
	-DSAVER_EXECUTABLE=\"@saver_executable@\" \
	-DPAM_SERVICE_NAME=\"@pam_service_name@\"
if PAM_CHECK_ACCOUNT_TYPE
macros += \
	-DPAM_CHECK_ACCOUNT_TYPE
endif
if HAVE_XSCREENSAVER_EXT
macros += -DHAVE_XSCREENSAVER_EXT
endif
if HAVE_XSYNC_EXT
macros += -DHAVE_XSYNC_EXT
endif
if HAVE_XCOMPOSITE_EXT
macros += -DHAVE_XCOMPOSITE_EXT
endif
if HAVE_XF86MISC_EXT
macros += -DHAVE_XF86MISC_EXT
endif
if HAVE_XFIXES_EXT
macros += -DHAVE_XFIXES_EXT
endif
if HAVE_XFT_EXT
macros += -DHAVE_XFT_EXT
endif
if HAVE_XRANDR_EXT
macros += -DHAVE_XRANDR_EXT
endif
if HAVE_XKB_EXT
macros += -DHAVE_XKB_EXT
endif

bin_PROGRAMS = \
	xsecurelock
xsecurelock_SOURCES = \
	auth_child.c auth_child.h \
	env_settings.c env_settings.h \
	logging.c logging.h \
	mlock_page.h \
	main.c \
	saver_child.c saver_child.h \
	unmap_all.c unmap_all.h \
	version.c version.h \
	wait_pgrp.c wait_pgrp.h \
	wm_properties.c wm_properties.h \
	xscreensaver_api.c xscreensaver_api.h
nodist_xsecurelock_SOURCES = \
	env_helpstr.inc
xsecurelock_CPPFLAGS = $(macros)

helpersdir = $(pkglibexecdir)
helpers_SCRIPTS = \
	helpers/saver_blank
if HAVE_HTPASSWD
helpers_SCRIPTS += \
	helpers/authproto_htpasswd
endif
if HAVE_MPLAYER
helpers_SCRIPTS += \
	helpers/saver_mplayer
endif
if HAVE_MPV
helpers_SCRIPTS += \
	helpers/saver_mpv
endif
if HAVE_PAMTESTER
helpers_SCRIPTS += \
	helpers/authproto_pamtester
endif
if HAVE_XSCREENSAVER
helpers_SCRIPTS += \
	helpers/saver_xscreensaver
endif

helpers_PROGRAMS = \
	saver_multiplex
saver_multiplex_SOURCES = \
	env_settings.c env_settings.h \
	helpers/monitors.c helpers/monitors.h \
	helpers/saver_multiplex.c \
	logging.c logging.h \
	saver_child.c saver_child.h \
	wait_pgrp.c wait_pgrp.h \
	wm_properties.c wm_properties.h \
	xscreensaver_api.c xscreensaver_api.h
saver_multiplex_CPPFLAGS = $(macros)

helpers_PROGRAMS += \
	dimmer
dimmer_SOURCES = \
	env_settings.c env_settings.h \
	helpers/dimmer.c \
	logging.c logging.h \
	wm_properties.c wm_properties.h
dimmer_CPPFLAGS = $(macros)

if HAVE_IDLE_TIMER
helpers_PROGRAMS += \
	until_nonidle
until_nonidle_SOURCES = \
	env_settings.c env_settings.h \
	helpers/until_nonidle.c \
	logging.c logging.h \
	wait_pgrp.c wait_pgrp.h
until_nonidle_CPPFLAGS = $(macros)
endif

helpers_PROGRAMS += \
	auth_x11
auth_x11_SOURCES = \
	env_info.c env_info.h \
	env_settings.c env_settings.h \
	helpers/authproto.c helpers/authproto.h \
	helpers/auth_x11.c \
	helpers/monitors.c helpers/monitors.h \
	logging.c logging.h \
	mlock_page.h \
	wait_pgrp.c wait_pgrp.h \
	xscreensaver_api.c xscreensaver_api.h
auth_x11_CPPFLAGS = $(macros) $(XFT_CFLAGS)
auth_x11_LDADD = $(XFT_LIBS)

if HAVE_PAM
helpers_PROGRAMS += \
	authproto_pam
authproto_pam_SOURCES = \
	env_info.c env_info.h \
	env_settings.c env_settings.h \
	helpers/authproto.c helpers/authproto.h \
	helpers/authproto_pam.c \
	logging.c logging.h \
	mlock_page.h
authproto_pam_CPPFLAGS = $(macros)
endif

docsdir = $(docdir)
docs_DATA = \
	CONTRIBUTING \
	LICENSE \
	README.md

# Some tools that we sure don't wan to install
noinst_PROGRAMS = cat_authproto nvidia_break_compositor get_compositor remap_all
cat_authproto_SOURCES = \
	logging.c logging.h \
	helpers/authproto.c helpers/authproto.h \
	test/cat_authproto.c
nvidia_break_compositor_SOURCES = \
	test/nvidia_break_compositor.c
nvidia_break_compositor_CPPFLAGS = $(macros)
get_compositor_SOURCES = \
	test/get_compositor.c
get_compositor_CPPFLAGS = $(macros)
remap_all_SOURCES= \
	test/remap_all.c \
	unmap_all.c unmap_all.h
remap_all_CPPFLAGS = $(macros)

FORCE:
version.c: FORCE
	if git describe --always --long >/dev/null 2>&1; then \
		echo "const char *const git_version = \"` \
			git describe --always --dirty --long \
		`\";" > version.c; \
	else \
		: version.c must exist in non-git builds.; \
		cat version.c; \
	fi
.PRECIOUS: version.c  # Old one is better than none.

env_helpstr.inc: README.md # Autogenerate for --help.
	grep -A 9999 'ENV VARIABLES START' "$<" |\
		grep -B 9999 'ENV VARIABLES END' |\
		grep '^[ *]' |\
		sed -e 's,\\\|",\\&,g; s,$$,\\n",; s,^,",;' > "$@"
main.$(OBJEXT): env_helpstr.inc

EXTRA_DIST = \
	CONTRIBUTING \
	LICENSE \
	README.md \
	autogen.sh \
	helpers/saver_blank \
	run-linters.sh \
	test \
	version.c

if HAVE_DOXYGEN
doxyfile.stamp:
	$(DOXYGEN) Doxyfile
	echo Timestamp > doxyfile.stamp

CLEANFILES = doxyfile.stamp

all-local: doxyfile.stamp

clean-local:
	$(RM) -r $(top_srcdir)/doxy
endif
