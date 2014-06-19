#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define LIBHOOK_DENY_PATH "/etc/proftpd/libhook.deny"
/* ZFS only: do not create and mount .snapshot if there is more than this number of parent directorries with non-root uid */
#define MAX_NONROOT_PARENT_DIRS_COUNT 1

typedef DIR *(*OPEN_T)(const char *name);
typedef struct dirent *(*READ_T)(DIR *dirp);
typedef int (*CHROOT_T)(const char *path);

static OPEN_T real_opendir = NULL;
static READ_T real_readdir = NULL;
static READ_T real_readdir64 = NULL;
static CHROOT_T real_chroot = NULL;


/* 
 *   readdir_override 
 *   0 - do nothing
 *   1 - netapp - .snapshot is hidden, we append it to last readdir call
 *       or hide when not allowed (zfs has visible .snapshot dirs)
*/
static int readdir_override = 0;
static int visible_snapshot = 0; 

static int allowed = 1;

DIR *opendir(const char *name) {
    void *handle = dlopen("/lib/x86_64-linux-gnu/libc.so.6", RTLD_LAZY);
    char *real_path;
    char buf[PATH_MAX+1];

    visible_snapshot=0;

    if (!real_opendir) real_opendir = (OPEN_T) dlsym(handle, "opendir");
    if (!real_readdir) real_readdir = (READ_T) dlsym(handle, "readdir");
    if (!real_readdir64) real_readdir64 = (READ_T) dlsym(handle, "readdir64");
    if (!real_chroot) real_chroot = (CHROOT_T) dlsym(handle, "chroot");

    real_path=realpath(name, NULL);
    /* check for .snapshot directory only at root path (/) */
    if (real_path && real_path[0]=='/' && real_path[1]=='\0') {
        readdir_override=0;
        /* we use access() to check if there is .snapshot */
        snprintf(buf, PATH_MAX, "%s/%s", real_path, ".snapshot");
        if (!access(buf, R_OK)) {
            readdir_override=1;
        }

        free(real_path);
    } else {
        /* realpath return NULL ? */
        readdir_override=0;
    }

    DIR *dirp = real_opendir(name);

    return dirp;
}



struct dirent *fake_readdir(DIR *dirp)
{
    /* statically allocated variables */
    /* fake dirent appended to end of list */
    static struct dirent fake_dirent;
    /* variable to check if we are at end of list */
    static int real_end=0; 

    struct dirent *ret;

    ret=real_readdir(dirp);

    if (ret==NULL) {
        /* if we are at end of list append fake dirent */
        if (readdir_override && !real_end && !visible_snapshot && allowed) {
            real_end=1;

            sprintf(fake_dirent.d_name, ".snapshot");
            fake_dirent.d_type=DT_DIR;

            return &fake_dirent;
        } else {
            return NULL;
        }
    } else {
        real_end=0;
        /* in case of zfs .snapshot dir is visible, so set variable and do not append fake one at end */
        if (!strncmp(ret->d_name, ".snapshot", 9)) {
            visible_snapshot=1;

            /* in case this path is not allowed, hide .snapshot (call next readdir) */
            if (!allowed) {
                syslog(LOG_DAEMON|LOG_ALERT, "zfs not allowed");
                return fake_readdir(dirp);
            }
        }
        return ret;
    }
}

struct dirent *readdir(DIR *dirp)
{
    return fake_readdir(dirp);
}

struct dirent *readdir64(DIR *dirp)
{
    return fake_readdir(dirp);
}

/* 
 * chroot hook
 *  - traverse path from end and check if there exists %s/.zfs/snapshot
 *  - in case we are on zfs check if chroot path has directories in individual snapshots
 *  - in case there are snapshots bind mount it to path/.snapshot/snapshot_name
 */
int chroot(const char *path) {
    char buf[PATH_MAX+1];
    char tmpbuf[PATH_MAX+1];
    char *ptr;
    int zfs=0;
    char zfsbasepath[PATH_MAX+1];
    char appendpath[PATH_MAX+1];
    char mkdirpath[PATH_MAX+1];
    struct stat statb;
    int nonroot_count=0;
    
    /* traverse path and check if there is .zfs/snapshot dir */
    strncpy(buf, path, PATH_MAX);

    while((ptr=strrchr(buf,'/'))) {
        *ptr='\0';

        stat(buf, &statb);
        if (statb.st_uid!=0) {
            nonroot_count++;
        }

        snprintf(tmpbuf, PATH_MAX, "%s%s", buf, "/.zfs/snapshot");
        syslog(LOG_DAEMON|LOG_ALERT, "tmpbuf: %s non-root-count: %d",  tmpbuf, nonroot_count);

        if (!access(tmpbuf, R_OK)) {
            zfs=1;
            strncpy(zfsbasepath, tmpbuf, PATH_MAX);
            strncpy(appendpath, path+strlen(buf)+1, PATH_MAX);
            break;
        }
    }

    
    allowed=check_if_allowed(path);

    if (zfs==1 && allowed && nonroot_count<=MAX_NONROOT_PARENT_DIRS_COUNT) {
        DIR *dirp;
        struct dirent *dp;

        syslog(LOG_DAEMON|LOG_ALERT, "zfsbasepath: %s %s",  zfsbasepath, appendpath);

        if ((dirp=real_opendir(zfsbasepath))!=NULL) {
            do {
                if ((dp = readdir(dirp)) != NULL) {
                    if (dp->d_type==DT_DIR && dp->d_name[0]!='.') {
                        snprintf(tmpbuf, PATH_MAX, "%s/%s/%s", zfsbasepath, dp->d_name, appendpath);

                        if (!access(tmpbuf, R_OK)) {
                            /* directory exist in snapshot, bind mount it*/

                            snprintf(mkdirpath, PATH_MAX, "%s/%s", path, ".snapshot");
                            if (access(mkdirpath, R_OK)) {
                                mkdir(mkdirpath, 0755);
                            }
                            syslog(LOG_DAEMON|LOG_ALERT, "mkdirpath: %s",  mkdirpath);

                            snprintf(mkdirpath, PATH_MAX, "%s/%s/%s", path, ".snapshot", dp->d_name);
                            if (access(mkdirpath, R_OK)) {
                                mkdir(mkdirpath, 0755);
                            }
                            syslog(LOG_DAEMON|LOG_ALERT, "mkdirpath: %s",  mkdirpath);
                            
                            /* check if dir is already mounted */
                            if (!check_if_mounted(mkdirpath)) {
                                syslog(LOG_DAEMON|LOG_ALERT, "mounting: %s -> %s",  tmpbuf, mkdirpath);
                                mount(tmpbuf, mkdirpath, "auto", MS_BIND, NULL);
                            } else {
                                syslog(LOG_DAEMON|LOG_ALERT, "already mounted: %s -> %s",  tmpbuf, mkdirpath);
                            }
                        }
                    }
                }
            } while (dp != NULL);
            closedir(dirp);
        }
    }

    return real_chroot(path);
}

/*
 * int check_if_mounted(path) - check if path is mounted from /proc/mounts
 * return 0 path is not mounted 
 * return 1 path is mounted 
 */

int check_if_mounted(char *path)
{
    FILE *fp;
    char buf[4096];
    int found=0;

    fp=fopen("/proc/mounts", "r");
    while (fgets(buf, 4096, fp)) {
        if (strstr(buf, path)) {
            found=1;
            break;
        }
    }
    
    fclose(fp);

    return found;
}

/*
 * int check_if_allowed(path) - check if path has allowed .snapshot visibility and mounting in case of zfs
 * return 0 if not allowed
 * return 1 otherwise
 */

int check_if_allowed(char *path) {
    FILE *fp;
    char buf[4096];
    int allowed=1;

    if (access(LIBHOOK_DENY_PATH, R_OK)) {
        return allowed;
    }

    fp=fopen(LIBHOOK_DENY_PATH, "r");
    while (fgets(buf, 4096, fp)) {
        /* remove ending newline */
        buf[strlen(buf)]='\0';
        if (strstr(path, buf)) {
            allowed=0;
            break;
        }

        /* in case string ALL we disallow */
        if (strstr(buf, "ALL")) {
            allowed=0;
            break;
        }
    }
    
    fclose(fp);

    return allowed;

}
