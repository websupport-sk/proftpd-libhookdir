#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef DIR *(*OPEN_T)(const char *name);
typedef struct dirent *(*READ_T)(DIR *dirp);

typedef int (*CHDIR_T)(const char *path);

static OPEN_T real_opendir = NULL;
static READ_T real_readdir = NULL;
static READ_T real_readdir64 = NULL;
static CHDIR_T real_chdir = NULL;

/* 
    override 
    0 - do nothing
    1 - netapp (append hidden .snapshot)
    2 - zfs (append hidden .zfs/snapshot as .snapshot, override when chdir)
*/
static int override = 0;

DIR *opendir(const char *name) {
    void *handle = dlopen("/lib/x86_64-linux-gnu/libc.so.6", RTLD_LAZY);
    char *real_path;
    char buf[PATH_MAX+1];

    if (!real_opendir) real_opendir = (OPEN_T) dlsym(handle, "opendir");
    if (!real_readdir) real_readdir = (READ_T) dlsym(handle, "readdir");
    if (!real_readdir64) real_readdir64 = (READ_T) dlsym(handle, "readdir64");
    if (!real_chdir) real_chdir = (CHDIR_T) dlsym(handle, "chdir");

    real_path=realpath(name, NULL);
    if (real_path && real_path[0]=='/' && real_path[1]=='\0') {
        override=0;
        /* we use access() to check if there is .snapshot or .zfs/snapshot directory */
        snprintf(buf, PATH_MAX, "%s/%s", real_path, ".snapshot");
        if (!access(buf, R_OK)) {
            override=1;
        } else {
            snprintf(buf, PATH_MAX, "%s/%s", real_path, ".zfs/snapshot");
            if (!access(buf, R_OK)) {
                override=2;
            }
        }

        free(real_path);
    } else {
        /* realpath return NULL ? */
        override=0;
    }

    DIR *dirp = real_opendir(name);

    return dirp;
}

struct dirent *readdir(DIR *dirp)
{
    /* statically allocated variables */
    /* fake dirent appended to end of list */
    static struct dirent fake_dirent;
    /* variable to check if we are at end of list */
    static int real_end=0; 

    struct dirent *ret;

    ret=real_readdir(dirp);

    if (ret==NULL) {
        /* if we are at end of list append our dirent */
        if (override && !real_end) {
            real_end=1;

            sprintf(fake_dirent.d_name, ".snapshot");
            fake_dirent.d_type=DT_DIR;

            return &fake_dirent;
        } else {
            return NULL;
        }
    } else {
        real_end=0;
        return ret;
    }
}

struct dirent *readdir64(DIR *dirp)
{
    /* statically allocated variables */
    /* fake dirent appended to end of list */
    static struct dirent fake_dirent;
    /* variable to check if we are at end of list */
    static int real_end=0; 

    struct dirent *ret;

    ret=real_readdir64(dirp);

    if (ret==NULL) {
        /* if we are at end of list append our dirent */
        if (override && !real_end) {
            real_end=1;

            sprintf(fake_dirent.d_name, ".snapshot");
            fake_dirent.d_type=DT_DIR;

            return &fake_dirent;
        } else {
            return NULL;
        }
    } else {
        real_end=0;
        return ret;
    }
}

int chdir (const char *path) {
    char newdir[PATH_MAX+1];
    char olddir[PATH_MAX+1];
    int ret;
    char *real_path;
    char cwd[PATH_MAX+1];
    char buf[PATH_MAX+1];

    void *handle = dlopen("/lib/x86_64-linux-gnu/libc.so.6", RTLD_LAZY);

    if (!real_opendir) real_opendir = (OPEN_T) dlsym(handle, "opendir");
    if (!real_readdir) real_readdir = (READ_T) dlsym(handle, "readdir");
    if (!real_readdir64) real_readdir64 = (READ_T) dlsym(handle, "readdir64");
    if (!real_chdir) real_chdir = (CHDIR_T) dlsym(handle, "chdir");

    /* if we are on zfs filesystem we replace .snapshot to .zfs/snapshot when chdir */
    if (override==2 && strlen(path)>9 && !strcmp(path + strlen(path) - 9, ".snapshot")) {
        strncpy(olddir, path, PATH_MAX);
        olddir[strlen(olddir)-9] = '\0';
        snprintf(newdir, PATH_MAX, "%s/.zfs/snapshot", olddir);
        ret=real_chdir(newdir);
    } else {
        ret=real_chdir(path);
    }

    return ret;
}


