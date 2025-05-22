#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>

#define DB_PATH "/system_data/priv/mms/app.db"
#define APPMETA_BASE "/user/appmeta"
#define LOG_FILE_PATH "/data/logs.txt"

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t* req, size_t size, int flags);

int file_exists(const char *path) {
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

void write_log(const char *fmt, ...) {
    FILE *log_file = fopen(LOG_FILE_PATH, "a");
    if (log_file == NULL) return;

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    fclose(log_file);
}

static void send_notification_fmt(const char *fmt, ...) {
    notify_request_t req = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, ap);
    va_end(ap);

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);

    va_list args_copy;
    va_start(args_copy, fmt);
    write_log(fmt, args_copy);
    va_end(args_copy);
}

int main() {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        send_notification_fmt("Failed to open database:❌ %s", sqlite3_errmsg(db));
        return 1;
    }

    const char *query = "SELECT rowid, icon0Info, logoImageInfo FROM tbl_conceptmetadata;";
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        send_notification_fmt("❌ Query failed: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int rowid = sqlite3_column_int(stmt, 0);
        const char *icon0_info = (const char *)sqlite3_column_text(stmt, 1);
        const char *logo_info = (const char *)sqlite3_column_text(stmt, 2);

        if (icon0_info && strstr(icon0_info, "/user/appmeta/")) {
            char icon0_copy[512];
            strncpy(icon0_copy, icon0_info, sizeof(icon0_copy));
            icon0_copy[sizeof(icon0_copy) - 1] = '\0';

            char *start = strstr(icon0_copy, "/user/appmeta/");
            if (start) {
                start += strlen("/user/appmeta/");
                char *end = strstr(start, "/icon0.png");
                if (end) {
                    char local_id[128] = {0};
                    strncpy(local_id, start, end - start);
                    local_id[end - start] = '\0';

                    char image_path[512];
                    snprintf(image_path, sizeof(image_path), "%s/%s/icon0.png", APPMETA_BASE, local_id);

                    sqlite3_stmt *update_stmt = NULL;

                    if (logo_info && strstr(logo_info, "/user/appmeta/")) {
                        // 
                        const char *null_sql = "UPDATE tbl_conceptmetadata SET logoImageInfo = NULL WHERE rowid = ?";
                        sqlite3_prepare_v2(db, null_sql, -1, &update_stmt, 0);
                        sqlite3_bind_int(update_stmt, 1, rowid);
                        if (sqlite3_step(update_stmt) == SQLITE_DONE) {
                            send_notification_fmt("The image has been removed from the class:🗑 %d", rowid);
                        }
                    } else if (!logo_info && file_exists(image_path)) {
                        // 
                        time_t now = time(NULL);
                        char new_logo_info[512];
                        snprintf(new_logo_info, sizeof(new_logo_info), "/user/appmeta/%s/icon0.png?ts=%ld", local_id, now);

                        const char *insert_sql = "UPDATE tbl_conceptmetadata SET logoImageInfo = ? WHERE rowid = ?";
                        sqlite3_prepare_v2(db, insert_sql, -1, &update_stmt, 0);
                        sqlite3_bind_text(update_stmt, 1, new_logo_info, -1, SQLITE_STATIC);
                        sqlite3_bind_int(update_stmt, 2, rowid);
                        if (sqlite3_step(update_stmt) == SQLITE_DONE) {
                            send_notification_fmt("A picture of the class has been added.✅ %d", rowid);
                        }
                    }

                    if (update_stmt)
                        sqlite3_finalize(update_stmt);
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    send_notification_fmt("The binary update of the images in the database has been completed✅");
    return 0;
}
