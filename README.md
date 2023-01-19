# fsarchive
Utility to archive Linux filesystem with libzip and bspatch; please note this is still in development and highly experimental.
This utility will primarily write zip files, starting with a _larger_ one and then potentially only writing _timestamped_ deltas, so that a given archive/backup will be relatively smaller at given points in times.

## Purpose
Main purpose of this utility is to have

* a relatively quick full backup creation
* fast backup updates/deltas over time
* ability to restore files with original timing, ownership and permissions
* usage of a common container (zip) format

## How does it work
_fsarchive_ would first create a zip archive of a given set of directories/files. As example:
```
./fsarchive -a /archive/path /home/user1
```
Would create timestamp archive(s) under `/archive/path` for all content (not sollowing _symlinks_) of `/home/user1`. At the end of this command one would have a zip file under `/archive/path` of the name as `fsarc_20230120_221035.zip`. The first time running this command the file would be quite sizeable, containing all the files/directories under `/home/user1`.
One week elapses, and we want to update the archive/backup. By executing again:
```
./fsarchive -a /archive/path /home/user1
```
A new file as `fsarc_20230127_190136.zip` would be created under `/archive/path`, this time contaning only the _delta_ and new files that have been changed over one week since the original archive.
This mechinism would work for as amny archives/snapshots will be created over time.

All the deltas would be in the form a full new files _or_ binary patches created through _bsdiff/bspatch_. If the latter case, only such information will be saved for changed files in delta archives, thus reducing the required space needed for such archive.

## Usage
As per _--help_ option:
```
Usage: ./fsarchive [options] dir1 dir2 ... 
Executes fsarchive 0.2.1

Archive options

-a, --archive (dir)     Archives all input files (dir1, dir2, ...) and directories inside
                        (dir)/fsarchive_<timestamp>.zip and/or updates existing archives generating a new
                        and/or delta (dir)/fsarchive_<timestamp>.zip
    --comp-level (l)    Sets the compression level to (l) (from 1 to 9) where 1 is fastest and 9 is best.
                        0 is default
-f, --comp-filter (f)   Excludes files from being compresses; this option follows same format as -x option
                        and can be repeated multiple times; files matching such expressions won't be compressed
                        Files that are excluded from compression are also excluded from bsdiff deltas
    --no-comp           Flag to create zip files without any compression - default off
    --force-new-arc     Flag to force the creation of a new archive (-a option) even if a previous already
                        exists (i.e. no delta archive would be created)
    --no-bsdiff         When creating delta archives do not store file differences as bsdiff/bspatch data
                        but simply store files with differences as new/full
-x, --exclude (str)     Excludes from archiving all the files/directories which match (str); if you want
                        to have a 'contain' search, do specify the "*(str)*" pattern (i.e. -x "*abc*"
                        will exclude all the files/dirs which contain the sequence 'abc').
                        If instead you want to specify a single token of characters, you can use '?'. This
                        wildcard is useful to specify specific directories/file names counts (i.e. the string
                        '/abc/?/?.jpg' will match all files/directories such as '/abc/d0/file0.jpg' but would
                        not match a name such as '/abc/def/d0/file0.jpg')
                        Please note that the only wildcards supported are * and ?, everything else will be
                        interpreted as a literal character.
                        You can specify multiple exclusions (i.e. -x ex1 -x ex2 ... )
    --size-filter (sz)  Set a maximum file size filter of size (sz); has to be a positive value (bytes) and
                        can have suffixes such as k, m and g to respectively interpret as KiB, MiB and GiB

Restore options

-r, --restore (arc)     Restores files from archive (arc) into current dir or ablsolute path if stored so
                        Specify -d to allow another directory to be the target destination for the restore
-d, --restore-dir (dir) Sets the restore directory to this location
    --no-metadata       Do not restore metadata (file/dir ownership, permission and times)

Generic options

-v, --verbose           Set log to maximum level
    --dry-run           Flag to execute the command as indicated without writing/amending any file/metadata
    --help              Prints this help and exit
```

## Technical description
Brief descriptions of archive and delta creations

### Zip creation/metadata
All the zip files are created with default compression options and deflate algorithm. _fsarchive_ leverages the zip format extension to store metadata, specifically for each file we store:
```c
typedef struct _stat64 {
	mode_t fs_mode;		// st_mode from lstat64
	uid_t fs_uid;		// st_uid from lstat64
	gid_t fs_gid;		// st_gid from lstat64
	uint32_t fs_type;	// fsarchive file type (new, unchanged, delta)
	time_t fs_atime;	// st_atime from lstat64
	time_t fs_mtime;	// st_mtime from lstat64
	time_t fs_ctime;	// st_ctime from lstat64
	off64_t fs_size;	// st_size from lstat64
	char	fs_prev[32];	// fsarchive previous archive to find unchanged file or file to apply a patch (can be recursive file1 --> patch0 --> patch1 ...)
} stat64_t;
```
In short, we save some fields from the output of [lstat64](https://linux.die.net/man/2/lstat64) and a specific couple ofr _fsarchive_ are added (see [zip_fs.h](https://github.com/Emanem/fsarchive/blob/main/src/zip_fs.h#L34)).
_libzip_ (and in general the zip format) already saves some metadata, but is not as accurate as the one returned by _lstat64_ (some time values are off by a second), hence the lstat64 data is used.

### bsdiff/bspatch usage
_bsdiff/bspatch_ are used to diff and then re-create files - see [fsarchive.cpp](https://github.com/Emanem/fsarchive/blob/main/src/fsarchive.cpp) for more insight).

### Memory requirements
Due to the above binary patching, the memory requirements when running _fsarchive_ are potentially high - one should have at least _+2x_ of largest file being archived of memory available when creating/restoring archives. For this reason, the options _-x_ and/or _--size-filter_ and/or _-f_ are quite handy.

## Sample usages
Archive all home directories, filtering files greater than 16 GiB, forcing the creation of a new _base_ archive, excluding the content of the _.cache_ subdirectories inside _home_:
```
sudo fsarchive --size-filter 16g --force-new-arc -x '/home/?/.cache/*' -a /archive/dir /home
```
Archive home directories, writing _delta_ if necessary, excluding caches and avoid compressing some files:
```
sudo fsarchive -f '*.jpg' -f '*.png' -x '/home/?/.cache/*' -a /archive/dir /home
```
Restore a given archive/snap not under the original path, but under a new location:
```
sudo fsarchive -r /archive/dir/fsarc_20230110_000056.zip -d /my/new/location
```

## Thanks
Thanks to:

* [libzip](https://libzip.org/) to manage zip archives
* [bsdiff/bspatch](https://github.com/mendsley/bsdiff) to create binary diff/patches

