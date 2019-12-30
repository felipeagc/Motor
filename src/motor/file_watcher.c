#include "../../include/motor/file_watcher.h"

#include "../../include/motor/arena.h"
#include "../../include/motor/array.h"

#if defined(__linux__)
#include <unistd.h>
#include <string.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <assert.h>
#include <sys/stat.h>

typedef struct WatcherItem {
    int wd;
    char *path;
} WatcherItem;

struct MtFileWatcher {
    MtArena *arena;
    int notifierfd;

    uint32_t watch_flags;

    /*array*/ WatcherItem *items;

    /*array*/ MtFileWatcherEvent *events;

    MtFileWatcherEvent *last_event;
};

static void watcher_add(MtFileWatcher *w, char *path) {
    int wd = inotify_add_watch(w->notifierfd, path, w->watch_flags);
    if (wd < 0) {
        return;
    }
    WatcherItem item = {.wd = wd, .path = mt_alloc(w->arena, strlen(path) + 1)};
    strncpy(item.path, path, strlen(path) + 1);
    mt_array_push(w->arena, w->items, item);
}
static void watcher_remove(MtFileWatcher *w, int wd) {
    for (size_t i = 0; i < mt_array_size(w->items); ++i) {
        if (wd != w->items[i].wd) continue;

        mt_free(w->arena, w->items[i].path);
        w->items[i].wd   = 0;
        w->items[i].path = 0x0;

        size_t swap_index = mt_array_size(w->items) - 1;
        if (i != swap_index)
            memcpy(w->items + i, w->items + swap_index, sizeof(WatcherItem));
        mt_array_set_size(w->items, mt_array_size(w->items) - 1);
        return;
    }
}

static void watcher_recursive_add(
    MtFileWatcher *w, char *path_buffer, size_t path_len, size_t path_max) {
    watcher_add(w, path_buffer);
    DIR *dirp = opendir(path_buffer);
    struct dirent *ent;
    while ((ent = readdir(dirp)) != 0x0) {
        if ((ent->d_type != DT_DIR && ent->d_type != DT_LNK &&
             ent->d_type != DT_UNKNOWN))
            continue;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        size_t d_name_size = strlen(ent->d_name);
        assert(path_len + d_name_size + 2 < path_max);

        strcpy(path_buffer + path_len, ent->d_name);
        path_buffer[path_len + d_name_size]     = '/';
        path_buffer[path_len + d_name_size + 1] = '\0';

        if (ent->d_type == DT_LNK || ent->d_type == DT_UNKNOWN) {
            struct stat statbuf;
            if (stat(path_buffer, &statbuf) == -1) continue;

            if (!S_ISDIR(statbuf.st_mode)) continue;
        }

        watcher_recursive_add(
            w, path_buffer, path_len + d_name_size + 1, path_max);
    }
    path_buffer[path_len] = '\0';

    closedir(dirp);
}

MtFileWatcher *mt_file_watcher_create(
    MtArena *arena, MtFileWatcherEventType types, const char *dir) {
    MtFileWatcher *w = mt_calloc(arena, sizeof(MtFileWatcher));
    w->arena         = arena;

    if (types & MT_FILE_WATCHER_EVENT_CREATE) w->watch_flags |= IN_CREATE;
    if (types & MT_FILE_WATCHER_EVENT_REMOVE) w->watch_flags |= IN_DELETE;
    if (types & MT_FILE_WATCHER_EVENT_MODIFY) w->watch_flags |= IN_MODIFY;
    if (types & MT_FILE_WATCHER_EVENT_MOVE) w->watch_flags |= IN_MOVE;

    w->watch_flags |= IN_DELETE_SELF;

    w->notifierfd = inotify_init1(IN_NONBLOCK);
    if (w->notifierfd == -1) {
        mt_free(arena, w);
        return NULL;
    }

    char path_buffer[4096];
    strncpy(path_buffer, dir, sizeof(path_buffer));

    size_t path_len = strlen(path_buffer);
    if (path_buffer[path_len - 1] != '/') {
        path_buffer[path_len]     = '/';
        path_buffer[path_len + 1] = '\0';
        ++path_len;
    }

    watcher_recursive_add(w, path_buffer, path_len, sizeof(path_buffer));

    return w;
}

static const char *find_wd_path(MtFileWatcher *w, int wd) {
    for (size_t i = 0; i < mt_array_size(w->items); ++i)
        if (wd == w->items[i].wd) return w->items[i].path;
    return 0x0;
}

static char *build_full_path(
    MtFileWatcher *watcher, int wd, const char *name, uint32_t name_len) {
    const char *dirpath = find_wd_path(watcher, wd);
    size_t dirlen       = strlen(dirpath);
    size_t length       = dirlen + 1 + name_len;
    char *res           = mt_alloc(watcher->arena, length + 1);
    if (res) {
        memcpy(res, dirpath, dirlen);
        memcpy(res + dirlen, name, name_len);
        res[length - 1] = 0;
    }
    return res;
}

bool mt_file_watcher_poll(MtFileWatcher *w, MtFileWatcherEvent *out_event) {
    if (w->last_event) {
        if (w->last_event->src) mt_free(w->arena, w->last_event->src);
        w->last_event->src = NULL;
        if (w->last_event->dst) mt_free(w->arena, w->last_event->dst);
        w->last_event->dst = NULL;
        w->last_event      = NULL;
    }

    char *move_src       = 0x0;
    uint32_t move_cookie = 0;

    char read_buffer[4096];
    ssize_t read_bytes = read(w->notifierfd, read_buffer, sizeof(read_buffer));
    if (read_bytes > 0) {
        for (char *bufp = read_buffer; bufp < read_buffer + read_bytes;) {
            struct inotify_event *ev = (struct inotify_event *)bufp;
            bool is_dir              = (ev->mask & IN_ISDIR);
            bool is_create           = (ev->mask & IN_CREATE);
            bool is_remove           = (ev->mask & IN_DELETE);
            bool is_modify           = (ev->mask & IN_MODIFY);
            bool is_move_from        = (ev->mask & IN_MOVED_FROM);
            bool is_move_to          = (ev->mask & IN_MOVED_TO);
            bool is_del_self         = (ev->mask & IN_DELETE_SELF);

            if (is_dir) {
                if (is_create) {
                    char *src = build_full_path(w, ev->wd, ev->name, ev->len);
                    char *add_src =
                        build_full_path(w, ev->wd, ev->name, ev->len);
                    watcher_add(w, add_src);
                    MtFileWatcherEvent e = {
                        MT_FILE_WATCHER_EVENT_CREATE, src, NULL};
                    mt_array_push(w->arena, w->events, e);
                } else if (is_remove) {
                    char *src = build_full_path(w, ev->wd, ev->name, ev->len);
                    MtFileWatcherEvent e = {
                        MT_FILE_WATCHER_EVENT_REMOVE, src, NULL};
                    mt_array_push(w->arena, w->events, e);
                } else if (is_del_self) {
                    watcher_remove(w, ev->wd);
                }
            } else if (ev->mask & IN_Q_OVERFLOW) {
                MtFileWatcherEvent e = {
                    MT_FILE_WATCHER_EVENT_BUFFER_OVERFLOW, NULL, NULL};
                mt_array_push(w->arena, w->events, e);
            } else {
                if (is_create) {
                    char *src = build_full_path(w, ev->wd, ev->name, ev->len);
                    MtFileWatcherEvent e = {
                        MT_FILE_WATCHER_EVENT_CREATE, src, NULL};
                    mt_array_push(w->arena, w->events, e);
                } else if (is_remove) {
                    char *src = build_full_path(w, ev->wd, ev->name, ev->len);
                    MtFileWatcherEvent e = {
                        MT_FILE_WATCHER_EVENT_REMOVE, src, NULL};
                    mt_array_push(w->arena, w->events, e);
                } else if (is_modify) {
                    char *src = build_full_path(w, ev->wd, ev->name, ev->len);
                    MtFileWatcherEvent e = {
                        MT_FILE_WATCHER_EVENT_MODIFY, src, NULL};
                    mt_array_push(w->arena, w->events, e);
                } else if (is_move_from) {
                    if (move_src != 0x0) {
                        // ... this is a new pair of a move, so the last one was
                        // move "outside" the current watch ...
                        char *src = mt_alloc(w->arena, strlen(move_src) + 1);
                        strncpy(src, move_src, strlen(move_src) + 1);
                        MtFileWatcherEvent e = {
                            MT_FILE_WATCHER_EVENT_MOVE, src, 0x0};
                        mt_array_push(w->arena, w->events, e);
                    }

                    // ... this is the first potential pair of a move ...
                    move_src    = build_full_path(w, ev->wd, ev->name, ev->len);
                    move_cookie = ev->cookie;
                } else if (is_move_to) {
                    if (move_src && move_cookie == ev->cookie) {
                        // ... this is the dst for a move ...
                        char *dst =
                            build_full_path(w, ev->wd, ev->name, ev->len);
                        MtFileWatcherEvent e = {
                            MT_FILE_WATCHER_EVENT_MOVE, move_src, dst};
                        mt_array_push(w->arena, w->events, e);

                        move_src    = 0x0;
                        move_cookie = 0;
                    } else if (move_src != 0x0) {
                        // ... this is a "move to outside of watch" ...
                        char *src = mt_alloc(w->arena, strlen(move_src) + 1);
                        strncpy(src, move_src, strlen(move_src) + 1);
                        MtFileWatcherEvent e = {
                            MT_FILE_WATCHER_EVENT_MOVE, src, 0x0};
                        mt_array_push(w->arena, w->events, e);

                        move_src    = 0x0;
                        move_cookie = 0;

                        // ...followed by a "move from outside to watch ...
                        char *dst =
                            build_full_path(w, ev->wd, ev->name, ev->len);
                        e = (MtFileWatcherEvent){
                            MT_FILE_WATCHER_EVENT_MOVE, NULL, dst};
                        mt_array_push(w->arena, w->events, e);
                    } else {
                        // ... this is a "move from outside to watch" ...
                        char *dst =
                            build_full_path(w, ev->wd, ev->name, ev->len);
                        MtFileWatcherEvent e = {
                            MT_FILE_WATCHER_EVENT_MOVE, NULL, dst};
                        mt_array_push(w->arena, w->events, e);
                    }
                }
            }

            bufp += sizeof(struct inotify_event) + ev->len;
        }

        if (move_src) {
            // ... we have a "move to outside of watch" that was never closed
            // ...
            char *src = mt_alloc(w->arena, strlen(move_src) + 1);
            strncpy(src, move_src, strlen(move_src) + 1);
            MtFileWatcherEvent e = {MT_FILE_WATCHER_EVENT_MOVE, src, 0x0};
            mt_array_push(w->arena, w->events, e);
        }
    }

    if (mt_array_size(w->events) == 0) {
        w->last_event = NULL;
        return false;
    }
    w->last_event = mt_array_pop(w->events);
    *out_event    = *w->last_event;
    return true;
}

void mt_file_watcher_destroy(MtFileWatcher *w) {
    close(w->notifierfd);

    for (uint32_t i = 0; i < mt_array_size(w->items); i++) {
        mt_free(w->arena, w->items[i].path);
    }

    for (uint32_t i = 0; i < mt_array_size(w->events); i++) {
        if (w->events[i].src) mt_free(w->arena, w->events[i].src);
        if (w->events[i].dst) mt_free(w->arena, w->events[i].dst);
    }

    mt_array_free(w->arena, w->items);
    mt_array_free(w->arena, w->events);
    mt_free(w->arena, w);
}
#endif
