/* ramfs.c - in-memory file system backend
 *
 * The file system is a tree of nodes allocated on the kernel heap. Every
 * directory keeps its children in a linked list sorted by name; every file
 * keeps its contents in a heap buffer that grows on demand. Nothing is
 * persisted: the tree is rebuilt from scratch on every boot.
 *
 * Operations run with interrupts disabled, so a task can never observe the
 * tree in a half-updated state (several tasks may use the FS at once).
 */

#include "fs/fs.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "drivers/rtc.h"
#include "arch/io.h"

typedef struct node {
    char         name[FS_NAME_MAX];
    fs_type_t    type;
    struct node* parent;
    struct node* children;      /* directories: first child */
    struct node* next;          /* next sibling */
    char*        data;          /* files: contents */
    uint32_t     size;
    uint32_t     cap;
    uint32_t     ctime, mtime;
} node_t;

static node_t* root = NULL;

/* ---------- internal helpers (call with interrupts disabled) ---------- */

static node_t* new_node(const char* name, fs_type_t type) {
    node_t* n = kzalloc(sizeof(node_t));
    if (!n) return NULL;
    strlcpy(n->name, name, FS_NAME_MAX);
    n->type = type;
    n->ctime = n->mtime = rtc_now();
    return n;
}

static node_t* find_child(node_t* dir, const char* name) {
    for (node_t* c = dir->children; c; c = c->next)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

static void insert_child(node_t* dir, node_t* child) {
    node_t** link = &dir->children;
    while (*link && strcmp((*link)->name, child->name) < 0) link = &(*link)->next;
    child->next = *link;
    *link = child;
    child->parent = dir;
    dir->mtime = rtc_now();
}

static void unlink_child(node_t* child) {
    node_t** link = &child->parent->children;
    while (*link != child) link = &(*link)->next;
    *link = child->next;
    child->parent->mtime = rtc_now();
}

/* Walk an absolute path. Returns FS_OK and sets *out, or an error. */
static int lookup(const char* path, node_t** out) {
    char norm[FS_PATH_MAX];
    int err = path_resolve("/", path, norm, sizeof(norm));
    if (err) return err;

    node_t* n = root;
    char* p = norm;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char* comp = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';

        if (n->type != FS_DIR) return FS_ENOTDIR;
        n = find_child(n, comp);
        if (!n) return FS_ENOENT;
    }
    *out = n;
    return FS_OK;
}

/* Find the parent directory of `path` and the name of the final component. */
static int lookup_parent(const char* path, node_t** parent, char* name) {
    char dir[FS_PATH_MAX];
    int err = path_split(path, dir, name);
    if (err) return err;
    err = lookup(dir, parent);
    if (err) return err;
    if ((*parent)->type != FS_DIR) return FS_ENOTDIR;
    return FS_OK;
}

static void fill_stat(node_t* n, fs_stat_t* st) {
    strlcpy(st->name, n == root ? "/" : n->name, FS_NAME_MAX);
    st->type = n->type;
    st->ctime = n->ctime;
    st->mtime = n->mtime;
    if (n->type == FS_FILE) {
        st->size = n->size;
    } else {
        st->size = 0;
        for (node_t* c = n->children; c; c = c->next) st->size++;
    }
}

static int create_node(const char* path, fs_type_t type, node_t** out) {
    node_t* parent;
    char name[FS_NAME_MAX];
    int err = lookup_parent(path, &parent, name);
    if (err) return err;
    if (find_child(parent, name)) return FS_EEXIST;

    node_t* n = new_node(name, type);
    if (!n) return FS_ENOMEM;
    insert_child(parent, n);
    if (out) *out = n;
    return FS_OK;
}

static void count(node_t* n, fs_info_t* info) {
    if (n->type == FS_DIR) {
        info->dirs++;
        for (node_t* c = n->children; c; c = c->next) count(c, info);
    } else {
        info->files++;
        info->bytes_used += n->size;
    }
}

/* ---------- public API ---------- */

int fs_init(void) {
    root = new_node("", FS_DIR);
    return root ? FS_OK : FS_ENOMEM;
}

int fs_stat(const char* path, fs_stat_t* st) {
    uint32_t f = irq_save();
    node_t* n;
    int err = lookup(path, &n);
    if (!err) fill_stat(n, st);
    irq_restore(f);
    return err;
}

int fs_mkdir(const char* path) {
    uint32_t f = irq_save();
    int err = create_node(path, FS_DIR, NULL);
    irq_restore(f);
    return err;
}

int fs_create(const char* path) {
    uint32_t f = irq_save();
    node_t* n;
    int err = lookup(path, &n);
    if (err == FS_OK)
        err = n->type == FS_DIR ? FS_EISDIR : FS_OK;
    else if (err == FS_ENOENT)
        err = create_node(path, FS_FILE, NULL);
    irq_restore(f);
    return err;
}

int fs_remove(const char* path) {
    uint32_t f = irq_save();
    node_t* n;
    int err = lookup(path, &n);
    if (!err && n == root) err = FS_EINVAL;
    if (!err && n->type == FS_DIR && n->children) err = FS_ENOTEMPTY;
    if (!err) {
        unlink_child(n);
        kfree(n->data);
        kfree(n);
    }
    irq_restore(f);
    return err;
}

int fs_write(const char* path, const void* data, uint32_t len, int append) {
    uint32_t f = irq_save();
    node_t* n;
    int err = lookup(path, &n);
    if (err == FS_ENOENT) err = create_node(path, FS_FILE, &n);
    if (!err && n->type == FS_DIR) err = FS_EISDIR;
    if (err) { irq_restore(f); return err; }

    uint32_t start = append ? n->size : 0;
    uint32_t needed = start + len;
    if (needed > n->cap) {
        uint32_t cap = n->cap ? n->cap : 64;
        while (cap < needed) cap *= 2;
        char* p = krealloc(n->data, cap);
        if (!p) { irq_restore(f); return FS_ENOMEM; }
        n->data = p;
        n->cap = cap;
    }
    memcpy(n->data + start, data, len);
    n->size = needed;
    n->mtime = rtc_now();
    irq_restore(f);
    return (int)len;
}

int fs_read(const char* path, uint32_t offset, void* buf, uint32_t len) {
    uint32_t f = irq_save();
    node_t* n;
    int err = lookup(path, &n);
    if (!err && n->type == FS_DIR) err = FS_EISDIR;
    if (err) { irq_restore(f); return err; }

    uint32_t avail = offset < n->size ? n->size - offset : 0;
    if (len > avail) len = avail;
    memcpy(buf, n->data + offset, len);
    irq_restore(f);
    return (int)len;
}

int fs_readdir(const char* path, uint32_t index, fs_stat_t* entry) {
    uint32_t f = irq_save();
    node_t* n;
    int err = lookup(path, &n);
    if (!err && n->type != FS_DIR) err = FS_ENOTDIR;
    if (!err) {
        node_t* c = n->children;
        while (c && index--) c = c->next;
        if (c) fill_stat(c, entry);
        else err = FS_ENOENT;
    }
    irq_restore(f);
    return err;
}

void fs_get_info(fs_info_t* info) {
    memset(info, 0, sizeof(*info));
    info->backend = "ramfs";
    uint32_t f = irq_save();
    count(root, info);
    irq_restore(f);
}
