libhookdir
==========

LD_PRELOAD library for proftpd used for netapp and zfs to show .snapshot and in case of zfs mount snapshots to user directory.

Prerequirements:
Proftpd must be configured to use chroot:
DefaultRoot ~

This library does 2 things

1. If there is hidden .snapshot directory show it (Netapp case).
It is usefull if you dont want to show .snapshot directly from netapp for every app because it can fuck up user scripts, backup scripts etc.
So we show it only in ftp/sftp/scp via proftpd.

2. If directory is under zfs filesystem, we find .zfs/snapshot directory in path (by traversing from end) and bind mount user directories to ftp root dir, so users can access it.
In this case library does not unmount snapshots, you must do it with different script.
It is done this way because zfs has only .zfs/snapshot at root volume directory.

In case we have 1 volume and more users we use "mount --bind" to mount directories from main .zfs/snapshots to user ftp root directory/.snapshot/...

Usage example:

gcc libhookdir.c -g -shared -fPIC -o libhookdir.so

add to /etc/default/proftpd:

export LD_PRELOAD=/usr/lib/proftpd/libhookdir.so proftpd


