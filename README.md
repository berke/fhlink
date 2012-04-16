fhlink
======

fhlink is a duplicate file finder and de-duplicator.  It finds files with
multiple copies and replaces them with hard links.

Attention has been paid to data structures and algorithms to keep the speed high
and the memory usage low.

It is written in C++ 0x.

Compilation
-----------
Make sure the autoreconf tool is available.  Under Debian Squeeze, it is
provided by the autoconf package.

Type
        autoreconf -i

This generates the familiar configuration script.  Type
        ./configure
        make
and you are set.

Operation
---------
fhlink will scan the given path, collecting all the relevant directory
information into memory.  Memory usage is about 100 bytes per eligible
file on 32-bit machines and 250 bytes on 64-bit machines for an average
filename length of 40 bytes.  Files appearing under multiple names (i.e.
hard-links) are registered only once.

fhlink will then determine files using an algorithm based on device ID and
size, a fast custom hash and finally content comparison.  Files residing
on different devices won't be considered equal.

Usage
-----
### --min-size <size-in-bytes>

Locating small files having identical content is generally not very useful
for reducing disk usage by hard-linking because of filesystem overhead.
In addition, small files can be much more numerous than larger files and
thus may take an inordinate amount processing of time for little benefit.

Specifing --min-size <size-in-bytes> limits the operation of fhlink to files
whose size equals or exceeds size-in-bytes.  The default is 100000 bytes.

### --dump

If the --dump option is given, it will then output the results to stdout
in the following format:
        duplicates <total-size> <single-size> <file-1> ... <file-n>
where <single-size> is the size of the n identical files in
bytes, <total-size> is n times <single-size>, and for each i, <file-i> is
the full name of the i-th file, enclosed in single quotes and properly escaped
for C-shells (i.e. a new line character is rendered as \n).

### --hard-link

(DISCLAIMER.  This option may cause fhlink to unpredictably delete and/or
hard-link files.  Only run it on backed-up data, and check the results
afterwards.)

If the --hard-link option is given, identical files residing on the
same device and having sufficient size w.r.t the --min-size option will be
turned into hard-links pointing to one of the files.

In other words, if you have n distinct files of identical contents f_1, ..., f_n
then f_2 to f_n will be deleted and replaced by hard-links to f_1.

This will save the space occupied by f_2 to f_n.

However, this operation has important drawbacks.

1. Without hardlinking, if one of the files is modified or corrupted, then the
other n - 1 copies will be intact.  With hardlinking, all will be modified or
corrupted.

2. All the hardlinked copies will physically be the same file (i-node) and
will thus have the same owner, permissions, access times and other metadata.

There is no option to instruct fhlink to pick a particular file as the "source
file".  Such a selection policy is difficult to define in a meaningful way.

Thus fhlink will pick one of the copies as the source file, typically the first
one encountered during the traversal.

### --chmod-clear <mask>

As a protection measure, fhlink will remove the write permissions on
the hard-linked files.  More specifically, the permissions bits specificied
in mask will be cleared from files that are merged by fhlink.  The default
value is 0222, i.e. write permissions will be cleared.

### --ignore-dirs <dirs>

Directories whose base name matches the given glob patterns will be
ignored.  Patterns are matched using fnmatch(3).

To specify a single pattern, just specify it after the option key --ignore-dirs.
(You will probably need to quote or escape the globbing characters.)

To specify multiple patterns <pattern_1> ... <pattern_n> enclose them between
empty strings, e.g. '' '.git' '.svn*' 'rc[0-9].d' ''

### --approximate

DANGEROUS in conjunction with --hard-links!  Disables the byte-by-byte
comparison and purely rely on file size and the custom hash function to
determine file equality.  Not recommended.  I'm not using a cryptographic
hash function because they are much slower than a simple, custom hash
function.  Also, hash functions get broken all the time these days and
if you keep example collisions on your drive, you may lose them.  Sure, salting
might have helped, but not with the lack of speed.

### --no-warnings

Suppress all warnings, such as warnings displayed when errors occur during
traversal or hardlinking.

### --no-progress

Disable the progress indicator.  Note: if the output is not a TTY, then
the progress indicator is disabled anyway.

### --debug

Print loads of debugging information.

Author
------
Oguz Berke Antoine DURAK <berke.durak@gmail.com>

License
-------
I've picked GPL3 today but if you need it under another license (BSD, etc.)
just let me know.
