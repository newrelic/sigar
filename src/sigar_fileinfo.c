/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000-2003 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 */

/*
 * whittled down version of apr/file_info/{unix,win32}/filestat.c
 * to fillin sigar_fileattrs_t
 */
#include "sigar_fileinfo.h"
#include "sigar_private.h"
#include "sigar_util.h"
#include "sigar_os.h"

static const char* types[] = {
    "none",
    "regular",
    "directory",
    "character device",
    "block device",
    "pipe",
    "symbolic link",
    "socket",
    "unknown"
};

SIGAR_DECLARE(const char *)
sigar_file_attrs_type_string_get(sigar_file_type_e type)
{
    if ((type < SIGAR_FILETYPE_NOFILE) ||
        (type > SIGAR_FILETYPE_UNKFILE))
    {
        type = SIGAR_FILETYPE_UNKFILE;
    }

    return types[type];
}

static const sigar_uint64_t perm_modes[] = {
    SIGAR_UREAD, SIGAR_UWRITE, SIGAR_UEXECUTE,
    SIGAR_GREAD, SIGAR_GWRITE, SIGAR_GEXECUTE,
    SIGAR_WREAD, SIGAR_WWRITE, SIGAR_WEXECUTE
};

static const char perm_chars[] = "rwx";

SIGAR_DECLARE(char *)
sigar_file_attrs_permissions_string_get(sigar_uint64_t permissions,
                                        char *str)
{
    char *ptr = str;
    int i=0, j=0;

    for (i=0; i<9; i+=3) {
        for (j=0; j<3; j++) {
            if (permissions & perm_modes[i+j]) {
                *ptr = perm_chars[j];
            }
            else {
                *ptr = '-';
            }
            ptr++;
        }
    }

    *ptr = '\0';
    return str;
}

static const int perm_int[] = {
    400, 200, 100,
     40,  20,  10,
      4,   2,   1
};

SIGAR_DECLARE(int)sigar_file_attrs_mode_get(sigar_uint64_t permissions)
{
    int i=0;
    int perms = 0;

    /* no doubt there is some fancy bitshifting
     * to convert, but this works fine.
     */
    for (i=0; i<9; i++) {
        if (permissions & perm_modes[i]) {
            perms += perm_int[i];
        }
    }

    return perms;
}

#define IS_DOTDIR(dir) \
    ((dir[0] == '.') && (!dir[1] || ((dir[1] == '.') && !dir[2])))

#ifdef WIN32

static void fillin_fileattrs(sigar_file_attrs_t *finfo,
                             WIN32_FILE_ATTRIBUTE_DATA *wininfo,
                             int linkinfo)
{
    DWORD *sizes = &wininfo->nFileSizeHigh;

    finfo->atime = FileTimeToTime(&wininfo->ftLastAccessTime) / 1000;
    finfo->ctime = FileTimeToTime(&wininfo->ftCreationTime) / 1000;
    finfo->mtime = FileTimeToTime(&wininfo->ftLastWriteTime) / 1000;

    finfo->size = (sigar_uint64_t)sizes[1];
    if (finfo->size < 0 || sizes[0]) {
        finfo->size = 0x7fffffff;
    }

    if (linkinfo &&
        (wininfo->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        finfo->type = SIGAR_FILETYPE_LNK;
    }
    else if (wininfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        finfo->type = SIGAR_FILETYPE_DIR;
    }
    else {
        finfo->type = SIGAR_FILETYPE_REG;
    }
}

static int fileattrs_get(sigar_t *sigar,
                         const char *file,
                         sigar_file_attrs_t *fileattrs,
                         int linkinfo)
{
    BY_HANDLE_FILE_INFORMATION info;
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    HANDLE handle;
    DWORD flags;

    SIGAR_ZERO(fileattrs);

    if (!GetFileAttributesExA(file,
                              GetFileExInfoStandard,
                              &attrs))
    {
        return GetLastError();
    }

    fillin_fileattrs(fileattrs, &attrs, linkinfo);

    flags = fileattrs->type == SIGAR_FILETYPE_DIR ?
        FILE_FLAG_BACKUP_SEMANTICS :
        FILE_ATTRIBUTE_NORMAL;

    handle = CreateFile(file,
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        flags,
                        NULL);
 
    if (handle != INVALID_HANDLE_VALUE) {
        if (GetFileInformationByHandle(handle, &info)) {
            fileattrs->inode =
                info.nFileIndexLow |
                (info.nFileIndexHigh << 32);
            fileattrs->device = info.dwVolumeSerialNumber;
            fileattrs->nlink  = info.nNumberOfLinks;
        }
        CloseHandle(handle);
    }

    return SIGAR_OK;
}

SIGAR_DECLARE(int) sigar_file_attrs_get(sigar_t *sigar,
                                        const char *file,
                                        sigar_file_attrs_t *fileattrs)
{
    return fileattrs_get(sigar, file, fileattrs, 0);
}

SIGAR_DECLARE(int) sigar_link_attrs_get(sigar_t *sigar,
                                        const char *file,
                                        sigar_file_attrs_t *fileattrs)
{
    return fileattrs_get(sigar, file, fileattrs, 1);
}

static __inline int file_type(char *file)
{
    WIN32_FILE_ATTRIBUTE_DATA attrs;

    if (!GetFileAttributesExA(file,
                              GetFileExInfoStandard,
                              &attrs))
    {
        return -1;
    }

    if (attrs.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        return SIGAR_FILETYPE_LNK;
    }
    else if (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        return SIGAR_FILETYPE_DIR;
    }
    else {
        return SIGAR_FILETYPE_REG;
    }
}

SIGAR_DECLARE(int) sigar_dir_stat_get(sigar_t *sigar,
                                      const char *dir,
                                      sigar_dir_stat_t *dirstats)
{
    char name[SIGAR_PATH_MAX+1];
    int len = strlen(dir);
    int max = sizeof(name)-len-1;
    char *ptr = name;
    WIN32_FIND_DATA data;
    HANDLE handle;
    DWORD error;
    char delim;

    SIGAR_ZERO(dirstats);
    if (file_type((char *)dir) != SIGAR_FILETYPE_DIR) {
        return ERROR_NO_MORE_FILES;
    }

    strncpy(name, dir, sizeof(name));
    ptr += len;
    if (strchr(dir, '/')) {
        delim = '/';
    }
    else {
        delim = '\\';
    }
    if (name[len] != delim) {
        *ptr++ = delim;
        len++;
        max--;
    }

    /* e.g. "C:\sigar\*" */
    name[len] = '*';
    name[len+1] = '\0';

    handle = FindFirstFile(name, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }

    do {
        /* skip '.' and '..' */
        if (IS_DOTDIR(data.cFileName)) {
            continue;
        }

        /* e.g. "C:\sigar\lib" */
        strncpy(ptr, data.cFileName, max);
        ptr[max] = '\0';

        switch (file_type(name)) {
          case -1:
            break;
          case SIGAR_FILETYPE_REG:
            ++dirstats->files;
            break;
          case SIGAR_FILETYPE_DIR:
            ++dirstats->subdirs;
            break;
          case SIGAR_FILETYPE_LNK:
            ++dirstats->symlinks;
            break;
          case SIGAR_FILETYPE_CHR:
            ++dirstats->chrdevs;
            break;
          case SIGAR_FILETYPE_BLK:
            ++dirstats->blkdevs;
            break;
          case SIGAR_FILETYPE_SOCK:
            ++dirstats->sockets;
            break;
          default:
            ++dirstats->total;
        }
    } while (FindNextFile(handle, &data));

    error = GetLastError();

    FindClose(handle);

    if (error != ERROR_NO_MORE_FILES) {
        return error;
    }

    dirstats->total +=
        dirstats->files +
        dirstats->subdirs +
        dirstats->symlinks +
        dirstats->chrdevs +
        dirstats->blkdevs +
        dirstats->sockets;

    return SIGAR_OK;
}

#else

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

static sigar_file_type_e filetype_from_mode(mode_t mode)
{
    sigar_file_type_e type;

    switch (mode & S_IFMT) {
    case S_IFREG:
        type = SIGAR_FILETYPE_REG;  break;
    case S_IFDIR:
        type = SIGAR_FILETYPE_DIR;  break;
    case S_IFLNK:
        type = SIGAR_FILETYPE_LNK;  break;
    case S_IFCHR:
        type = SIGAR_FILETYPE_CHR;  break;
    case S_IFBLK:
        type = SIGAR_FILETYPE_BLK;  break;
#if defined(S_IFFIFO)
    case S_IFFIFO:
        type = SIGAR_FILETYPE_PIPE; break;
#endif
#if !defined(BEOS) && defined(S_IFSOCK)
    case S_IFSOCK:
        type = SIGAR_FILETYPE_SOCK; break;
#endif

    default:
	/* Work around missing S_IFxxx values above
         * for Linux et al.
         */
#if !defined(S_IFFIFO) && defined(S_ISFIFO)
    	if (S_ISFIFO(mode)) {
            type = SIGAR_FILETYPE_PIPE;
	} else
#endif
#if !defined(BEOS) && !defined(S_IFSOCK) && defined(S_ISSOCK)
    	if (S_ISSOCK(mode)) {
            type = SIGAR_FILETYPE_SOCK;
	} else
#endif
        type = SIGAR_FILETYPE_UNKFILE;
    }
    return type;
}

static sigar_uint64_t sigar_unix_mode2perms(mode_t mode)
{
    sigar_uint64_t perms = 0;

    if (mode & S_IRUSR)
        perms |= SIGAR_UREAD;
    if (mode & S_IWUSR)
        perms |= SIGAR_UWRITE;
    if (mode & S_IXUSR)
        perms |= SIGAR_UEXECUTE;

    if (mode & S_IRGRP)
        perms |= SIGAR_GREAD;
    if (mode & S_IWGRP)
        perms |= SIGAR_GWRITE;
    if (mode & S_IXGRP)
        perms |= SIGAR_GEXECUTE;

    if (mode & S_IROTH)
        perms |= SIGAR_WREAD;
    if (mode & S_IWOTH)
        perms |= SIGAR_WWRITE;
    if (mode & S_IXOTH)
        perms |= SIGAR_WEXECUTE;

    return perms;
}

static SIGAR_INLINE void copy_stat_info(sigar_file_attrs_t *fileattrs,
                                        struct stat *info)
{
    fileattrs->permissions = sigar_unix_mode2perms(info->st_mode);
    fileattrs->type        = filetype_from_mode(info->st_mode);
    fileattrs->uid         = info->st_uid;
    fileattrs->gid         = info->st_gid;
    fileattrs->size        = info->st_size;
    fileattrs->inode       = info->st_ino;
    fileattrs->device      = info->st_dev;
    fileattrs->nlink       = info->st_nlink;
    fileattrs->atime       = info->st_atime;
    fileattrs->mtime       = info->st_mtime;
    fileattrs->ctime       = info->st_ctime;
    fileattrs->atime *= 1000;
    fileattrs->mtime *= 1000;
    fileattrs->ctime *= 1000;
}

int sigar_file_attrs_get(sigar_t *sigar,
                         const char *file,
                         sigar_file_attrs_t *fileattrs)
{
    struct stat info;

    if (stat(file, &info) == 0) {
        copy_stat_info(fileattrs, &info);
        return SIGAR_OK;
    }
    else {
        return errno;
    }
}

int sigar_link_attrs_get(sigar_t *sigar,
                         const char *file,
                         sigar_file_attrs_t *fileattrs)
{
    struct stat info;

    if (lstat(file, &info) == 0) {
        copy_stat_info(fileattrs, &info);
        return SIGAR_OK;
    }
    else {
        return errno;
    }
}

int sigar_dir_stat_get(sigar_t *sigar,
                       const char *dir,
                       sigar_dir_stat_t *dirstats)
{
    char name[SIGAR_PATH_MAX+1];
    int len = strlen(dir);
    int max = sizeof(name)-len-1;
    char *ptr = name;
    DIR *dirp = opendir(dir);
    struct dirent *ent;
    struct stat info;
#ifdef HAVE_READDIR_R
    struct dirent dbuf;
#endif

    if (!dirp) {
        return errno;
    }

    SIGAR_ZERO(dirstats);    

    strncpy(name, dir, sizeof(name));
    ptr += len;
    if (name[len] != '/') {
        *ptr++ = '/';
        len++;
        max--;
    }

#ifdef HAVE_READDIR_R
    while (readdir_r(dirp, &dbuf, &ent) == 0) {
        if (ent == NULL) {
            break;
        }
#else
    while ((ent = readdir(dirp))) {
#endif
        /* skip '.' and '..' */
        if (IS_DOTDIR(ent->d_name)) {
            continue;
        }

        strncpy(ptr, ent->d_name, max);
        ptr[max] = '\0';

        if (lstat(name, &info) != 0) {
            continue;
        }

        switch (filetype_from_mode(info.st_mode)) {
          case SIGAR_FILETYPE_REG:
            ++dirstats->files;
            break;
          case SIGAR_FILETYPE_DIR:
            ++dirstats->subdirs;
            break;
          case SIGAR_FILETYPE_LNK:
            ++dirstats->symlinks;
            break;
          case SIGAR_FILETYPE_CHR:
            ++dirstats->chrdevs;
            break;
          case SIGAR_FILETYPE_BLK:
            ++dirstats->blkdevs;
            break;
          case SIGAR_FILETYPE_SOCK:
            ++dirstats->sockets;
            break;
          default:
            ++dirstats->total;
        }
    }

    dirstats->total +=
        dirstats->files +
        dirstats->subdirs +
        dirstats->symlinks +
        dirstats->chrdevs +
        dirstats->blkdevs +
        dirstats->sockets;

    closedir(dirp);

    return SIGAR_OK;
}

#endif
