#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <ps5/kernel.h> // Make sure this file is in the SDK and contains sceKernelSendNotificationRequest

#define DB_PATH "/system_data/priv/mms/app.db"
#define APPMETA_BASE "/user/appmeta"

int file_exists(const char *path) {
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

// PS5 Notification Struct & Function
typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t* req, size_t size, int flags);

// Log History Path
#define LOG_FILE_PATH "/data/logs.txt"

// Function to write messages to the log
void write_log(const char *fmt, ...) {
    FILE *log_file = fopen(LOG_FILE_PATH, "a");
    if (log_file == NULL) {
        // If we fail to open the file, we cannot record the log.
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);  // write to log
    va_end(args);
    
    fclose(log_file);  // Close file after writing
}

// Function to send PS5 notification
static void send_notification_fmt(const char *fmt, ...) {
    notify_request_t req = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, ap);
    va_end(ap);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);

    // Now we copy the list of parameters to another variable so that we do not lose it after using it in vsnprintf.
    va_list args_copy;
    va_copy(args_copy, ap);
    write_log(fmt, args_copy);
    va_end(args_copy);
}

int main() {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Database cannot be opened:❌ %s\n", sqlite3_errmsg(db));
        return 1;
    }

    const char *query = "SELECT rowid, icon0Info FROM tbl_conceptmetadata;";
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Query failed:❌ %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int rowid = sqlite3_column_int(stmt, 0);
        const unsigned char *icon0_info = sqlite3_column_text(stmt, 1);

        if (icon0_info && strstr((const char *)icon0_info, "/user/appmeta/")) {
            char icon0_copy[512];
            strncpy(icon0_copy, (const char *)icon0_info, sizeof(icon0_copy));
            icon0_copy[sizeof(icon0_copy) - 1] = '\0';

            char *start = strstr(icon0_copy, "/user/appmeta/");
            if (start) {
                start += strlen("/user/appmeta/");
                char *end = strstr(start, "/icon0.png");
                if (end) {
                    char local_id[128] = {0};
                    size_t len = end - start;
                    strncpy(local_id, start, len);
                    local_id[len] = '\0';

                    char image_path[512];
                    snprintf(image_path, sizeof(image_path), "%s/%s/icon0.png", APPMETA_BASE, local_id);

                    if (file_exists(image_path)) {
                        time_t now = time(NULL);
                        char new_logo_info[512];
                        snprintf(new_logo_info, sizeof(new_logo_info), "/user/appmeta/%s/icon0.png?ts=%ld", local_id, now);

                        sqlite3_stmt *update_stmt;
                        const char *update_sql = "UPDATE tbl_conceptmetadata SET logoImageInfo = ? WHERE rowid = ?";
                        sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, 0);
                        sqlite3_bind_text(update_stmt, 1, new_logo_info, -1, SQLITE_STATIC);
                        sqlite3_bind_int(update_stmt, 2, rowid);

                        if (sqlite3_step(update_stmt) == SQLITE_DONE) {
                            printf("Update the class✅ %d Using: %s\n", rowid, new_logo_info);

                            // Send notification
                            char notif[256];
                            snprintf(notif, sizeof(notif), "The class has been updated %d Successfully ✅", rowid);
                            send_notification_fmt(notif);
                        } else {
                            fprintf(stderr, "Error updating the row ❌ %d\n", rowid);
                        }

                        sqlite3_finalize(update_stmt);
                    } else {
                        printf("Image not found: ❌ %s\n", image_path);
                    }
                } else {
                    printf("Not found ⚠️ /icon0.png On path: %s\n", icon0_info);
                }
            }
        } else {
            printf("⚠️ icon0Info Invalid or does not contain a path appmeta -- value: %s\n", icon0_info ? (const char *)icon0_info : "NULL");
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    send_notification_fmt("✅ The database update has been completed successfully.");
    printf("✅ The update is complete.\n");

    return 0;
}
