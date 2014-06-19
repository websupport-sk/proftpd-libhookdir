libhookdir
==========

LD_PRELOAD library for proftpd used for netapp and zfs to show .snapshot and other stuff.

Prerequirements:
Proftpd must be configured to use chroot:
DefaultRoot ~

This library does 2 things

1. If there is hidden .snapshot directory show it (Netapp case)

2. If directory is under zfs filesystem, we find .zfs/snapshot directory in path and bind mount user directories to ftp root dir, so users can access it.
In this case library does not unmount snapshots, you must do it with different script.
