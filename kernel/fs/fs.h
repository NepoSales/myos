/* fs.h - file system interface
 *
 * A small path-based API, similar in spirit to a VFS layer: the shell and
 * the programs only talk to these functions, never to the storage backend
 * directly. Paths must be absolute ("/home/notes.txt"); use path_resolve()
 * to turn a relative path into an absolute one.
 *
 * All functions return 0 (or a byte count) on success and a negative
 * FS_E* error code on failure.
 */
#ifndef FS_FS_H
#define FS_FS_H

#include <stdint.h>
#include <stddef.h>

#define FS_NAME_MAX   28        /* max file name length, including the NUL */
#define FS_PATH_MAX   128

enum {
    FS_OK           =  0,
    FS_ENOENT       = -1,       /* no such file or directory */
    FS_EEXIST       = -2,       /* already exists */
    FS_ENOTDIR      = -3,       /* a path component is not a directory */
    FS_EISDIR       = -4,       /* is a directory */
    FS_ENOTEMPTY    = -5,       /* directory not empty */
    FS_ENOMEM       = -6,       /* out of memory */
    FS_EINVAL       = -7,       /* invalid argument */
    FS_ENOSPC       = -8,       /* no space left on device */
    FS_ENAMETOOLONG = -9,       /* name or path too long */
    FS_EIO          = -10,      /* device error */
};

typedef enum { FS_FILE = 1, FS_DIR = 2 } fs_type_t;

typedef struct {
    char      name[FS_NAME_MAX];
    fs_type_t type;
    uint32_t  size;             /* bytes for files, number of entries for directories */
    uint32_t  ctime;            /* creation time (Unix timestamp) */
    uint32_t  mtime;            /* last modification time (Unix timestamp) */
} fs_stat_t;

typedef struct {
    const char* backend;        /* e.g. "ramfs" */
    uint32_t    files;
    uint32_t    dirs;
    uint32_t    bytes_used;     /* file contents */
    uint32_t    bytes_total;    /* capacity, or 0 if only limited by RAM */
} fs_info_t;

int  fs_init(void);

int  fs_stat(const char* path, fs_stat_t* st);
int  fs_mkdir(const char* path);
int  fs_create(const char* path);           /* create an empty file (no-op if it exists) */
int  fs_remove(const char* path);           /* file or empty directory */

/* Write `len` bytes, replacing the file contents (or appending to them).
 * The file is created if it does not exist. */
int  fs_write(const char* path, const void* data, uint32_t len, int append);

/* Read up to `len` bytes starting at `offset`. Returns the bytes read. */
int  fs_read(const char* path, uint32_t offset, void* buf, uint32_t len);

/* Get the `index`-th entry of a directory (sorted by name).
 * Returns FS_OK, or FS_ENOENT past the last entry. */
int  fs_readdir(const char* path, uint32_t index, fs_stat_t* entry);

void fs_get_info(fs_info_t* info);
const char* fs_strerror(int err);

/* ---------- path helpers (fs/path.c) ---------- */

/* Combine `cwd` and `path` into a normalized absolute path in `out`,
 * resolving "." and ".." and duplicate slashes. */
int  path_resolve(const char* cwd, const char* path, char* out, size_t out_size);

/* Split an absolute path into its parent directory and final name. */
int  path_split(const char* path, char* parent, char* name);

#endif
