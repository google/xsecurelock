#!/bin/sh

set -ex

thisversion=$(grep '^define(\[thisversion\],\[' configure.ac | cut -d '[' -f 3 | cut -d ']' -f 1)
nextversion=$(grep '^define(\[nextversion\],\[' configure.ac | cut -d '[' -f 3 | cut -d ']' -f 1)
if [ x"$thisversion" = x"$nextversion" ]; then
	echo >&2 "Please bump nextversion in configure.ac first!"
	echo >&2 "Follow semantic versioning when doing so."
	echo >&2 "Changes since last version:"
	git log v"$thisversion".. >&2
	exit 1
fi

# Inform caller about semver requirements.
if git grep "REMOVE IN v${nextversion%%.*}\\>" .; then
	echo >&2 'Stuff to do before next semver!'
	exit 1
fi

# Are we on master?
if [ x"$(git rev-parse --abbrev-ref HEAD)" != x'master' ]; then
	echo >&2 'Must be on master.'
	exit 1
fi

# Bump the version.
sed -i -e '
	s/^define(\[thisversion\],\[.*/define([thisversion],['"$nextversion"'])/
' configure.ac

# Generate a tarball.
tardir=xsecurelock-$nextversion
tarball=$tardir.tar.gz
rm -f "$tarball"
./config.status --recheck
make clean
rm -f version.c
make dist GIT_VERSION=v$nextversion
ls -l "$tarball"

# Extra stuff added by "make dist".
tar_extra="
Makefile.in
aclocal.m4
compile
configure
depcomp
install-sh
missing
version.c
"

# Stuff that only makes sense to have in git.
git_extra="
.gitignore
build-release.sh
"

# Compare tarball to git file listing.
tar_content=$(tar tf "$tarball" | cut -d / -f 2- | grep '[^/]$')
git_content=$(git ls-files -c)
tar_only=$(
	{
		echo "$tar_content"
		echo "$tar_extra"
		echo "$tar_extra"
		echo "$git_content"
		echo "$git_content"
	} | sort | uniq -u
)
git_only=$(
	{
		echo "$tar_content"
		echo "$tar_content"
		echo "$git_extra"
		echo "$git_extra"
		echo "$git_content"
	} | sort | uniq -u
)
if [ -n "$tar_only" ]; then
	echo >&2 "tar only: " $tar_only
	exit 1
fi
if [ -n "$git_only" ]; then
	echo >&2 "git only: " $git_only
	exit 1
fi

# Do a test build.
rm -rf "$tardir"
tar xvf "$tarball"
cd "$tardir"
grep "$nextversion" version.c
./configure --with-pam-service-name=common-auth
make
make install DESTDIR=$PWD/tmp
find tmp
cd ..
rm -rf "$tardir"

# Confirm OK.
set +x
echo 'You may now tag the release:'
echo "  git commit -a -m 'Bump version to $nextversion.'"
echo "  git tag -a v$nextversion"
echo "  git push origin HEAD"
echo "  git push origin tag v$nextversion"
echo "Then upload $tarball to github."
