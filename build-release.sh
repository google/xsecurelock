#!/bin/sh

set -ex

new_version=$1

# Need the new version.
if [ -z "$new_version" ]; then
	echo >&2 "Usage: $0 new-version-number"
	exit 1
fi

# Are we on master?
if [ x"$(git rev-parse --abbrev-ref HEAD)" != x'master' ]; then
	echo >&2 'Must be on master.'
	exit 1
fi

# Bump the version.
sed -i -e "
	s,\\(AC_INIT(\\[xsecurelock\\]\\,\\[\\)[^]]*,\\1$new_version,
" configure.ac

# Generate a tarball.
tardir=xsecurelock-$new_version
tarball=$tardir.tar.gz
rm -f "$tarball"
./config.status --recheck
make clean
rm -f version.c
make dist GIT_VERSION=v$new_version
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
run-iwyu.sh
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
grep "$new_version" version.c
./configure --with-pam-service-name=common-auth
make
make install DESTDIR=$PWD/tmp
find tmp
cd ..
rm -rf "$tardir"

# Confirm OK.
set +x
echo 'You may now tag the release:'
echo "  git commit -a -m 'Bump version to $new_version.'"
echo "  git tag -a v$new_version"
echo "  git push origin HEAD"
echo "  git push origin tag v$new_version"
echo "Then upload $tarball to github."
