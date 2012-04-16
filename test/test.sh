#!/bin/bash

set -e

dir="/tmp/test-dir-$$.$RANDOM"

echo "$0: Test dir is $dir"

if [ -e "$dir" ]; then
        echo "$0: Test dir $dir already exists!" 2>&1
        exit 1
fi

./mktestdir.sh "$dir"
du -s "$dir" >"$dir.before.size"
find "$dir" -type f -print0 | xargs -0 md5sum|sort >"$dir.before"
../src/fhlink --dump --hard-link --chmod-clear 0222 "$dir" >"$dir.dump"
find "$dir" -type f -print0 | xargs -0 md5sum|sort >"$dir.after"
du -s "$dir" >"$dir.after.size"

if cmp -s "$dir.before" "$dir.after" ; then
        echo "$0: PASS"
else
        echo "$0: TEST FAILED!" 2>&1
        exit 2
fi
