#!/bin/sh
#
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

# Simple script to ensure all settings variables are documented.

# List all settings (usually from Get*Settings call).
all_settings=$(
	for file in *.[ch] */*.[ch] */auth_* */saver_*; do
		<"$file" perl -ne '
			print "$_\n" for /\bXSECURELOCK_[A-Za-z0-9_%]+\b/g;
		'
	done | sort -u
)

# List of internal settings. These shall not be documented.
internal_settings='
XSECURELOCK_INSIDE_SAVER_MULTIPLEX
'

# List of deprecated settings. These shall not be documented.
deprecated_settings='
XSECURELOCK_WANT_FIRST_KEYPRESS
'

public_settings=$(
	{
		echo "$all_settings"
		echo "$internal_settings"
		echo "$internal_settings"
		echo "$deprecated_settings"
		echo "$deprecated_settings"
	} | sort | uniq -u
)

# List all documented settings.
documented_settings=$(
	<README.md perl -ne '
		if (/ENV VARIABLES START/../ENV VARIABLES_END/) {
			print "$_\n" for /^\* +\`(XSECURELOCK_[A-Za-z0-9_%]+)\`/g;
		}
	' | sort -u
)

status=0

undocumented_settings=$(
	{
		echo "$public_settings"
		echo "$documented_settings"
		echo "$documented_settings"
	} | sort | uniq -u
)
if [ -n "$undocumented_settings" ]; then
	echo "The following settings lack documentation:"
	echo "$undocumented_settings"
	echo
	status=1
fi

gone_settings=$(
	{
		echo "$public_settings"
		echo "$public_settings"
		echo "$documented_settings"
	} | sort | uniq -u
)
if [ -n "$gone_settings" ]; then
	echo "The following documented settings don't exist:"
	echo "$gone_settings"
	echo
	status=1
fi

exit $status
