#!/bin/bash

target="$1"

if [ -z "$target" ]; then
        echo "$0: No target specified." >&2
        exit 1
fi

mkdir -p "$target"
counter=0

next_path()
{
        counter=$(( counter + 1 ))
        dir="$target/$(dd if=/dev/urandom bs=10 count=1 \
                if=/dev/urandom 2>/dev/null |
                tr -d -c 'a-z0-9/' |
                tr '0-9' '/' |
                tr 'a-m' 0 |
                tr 'n-z' 1 |
                sed -e 's,//*,/,g' |
                sed -e 's,^/,,' -e 's,/$,,')"
        path="$dir/f.$counter"
        #echo "$counter $path"
}

for x in `seq 1 100` ; do
        next_path
        dir_0="$dir"
        path_0="$path"
        counter_0=$counter
        sz=$(( 1 + RANDOM % 100 ))
        mkdir -p "$dir_0"
        dd if=/dev/urandom of="$path_0" bs=1k count=$sz >/dev/null 2>&1

        n=$(( 1 + RANDOM % 20 ))
        for y in `seq 1 $n` ; do
                next_path
                path_i="$path"
                dir_i="$dir"
                mkdir -p "$dir_i"
                if [ $(( RANDOM % 100 )) -gt 50 ] ; then
                        cp "$path_0" "$path_i.c.$counter_0"
                else
                        cp -l "$path_0" "$path_i.l.$counter_0"
                fi
        done
done
