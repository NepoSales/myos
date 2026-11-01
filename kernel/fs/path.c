/* path.c - path parsing shared by every file system backend */

#include "fs/fs.h"
#include "lib/string.h"

#define MAX_DEPTH 32

int path_resolve(const char* cwd, const char* path, char* out, size_t out_size) {
    char buf[FS_PATH_MAX * 2];
    const char* parts[MAX_DEPTH];
    int depth = 0;

    /* concatenate "cwd/path" (or just "path" if it is absolute) */
    if (path[0] == '/') {
        if (strlcpy(buf, path, sizeof(buf)) >= sizeof(buf)) return FS_ENAMETOOLONG;
    } else {
        if (strlcpy(buf, cwd, sizeof(buf)) >= sizeof(buf)) return FS_ENAMETOOLONG;
        strlcat(buf, "/", sizeof(buf));
        if (strlcat(buf, path, sizeof(buf)) >= sizeof(buf)) return FS_ENAMETOOLONG;
    }

    /* split on '/', handling "." and ".." with a stack of components */
    char* p = buf;
    while (*p) {
        while (*p == '/') *p++ = '\0';
        if (!*p) break;
        char* comp = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';

        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            if (depth > 0) depth--;
            continue;
        }
        if (strlen(comp) >= FS_NAME_MAX) return FS_ENAMETOOLONG;
        if (depth == MAX_DEPTH) return FS_ENAMETOOLONG;
        parts[depth++] = comp;
    }

    /* join the components back together */
    if (out_size < 2) return FS_ENAMETOOLONG;
    out[0] = '\0';
    if (depth == 0) {
        strlcpy(out, "/", out_size);
        return FS_OK;
    }
    for (int i = 0; i < depth; i++) {
        if (strlcat(out, "/", out_size) >= out_size) return FS_ENAMETOOLONG;
        if (strlcat(out, parts[i], out_size) >= out_size) return FS_ENAMETOOLONG;
    }
    return FS_OK;
}

int path_split(const char* path, char* parent, char* name) {
    char norm[FS_PATH_MAX];
    int err = path_resolve("/", path, norm, sizeof(norm));
    if (err) return err;
    if (strcmp(norm, "/") == 0) return FS_EINVAL;      /* the root has no parent */

    char* slash = strrchr(norm, '/');
    strlcpy(name, slash + 1, FS_NAME_MAX);
    if (slash == norm) {
        strlcpy(parent, "/", FS_PATH_MAX);
    } else {
        *slash = '\0';
        strlcpy(parent, norm, FS_PATH_MAX);
    }
    return FS_OK;
}

const char* fs_strerror(int err) {
    switch (err) {
    case FS_OK:           return "success";
    case FS_ENOENT:       return "no such file or directory";
    case FS_EEXIST:       return "file exists";
    case FS_ENOTDIR:      return "not a directory";
    case FS_EISDIR:       return "is a directory";
    case FS_ENOTEMPTY:    return "directory not empty";
    case FS_ENOMEM:       return "out of memory";
    case FS_EINVAL:       return "invalid argument";
    case FS_ENOSPC:       return "no space left on device";
    case FS_ENAMETOOLONG: return "name too long";
    case FS_EIO:          return "I/O error";
    default:              return "unknown error";
    }
}
