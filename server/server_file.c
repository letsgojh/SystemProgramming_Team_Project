#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "protocol.h"

extern void server_log(const char *fmt, ...);

// 서버 파일 저장 디렉토리
#define STORAGE_DIR "./server/server_storage/"

// 삭제 타이머 스레드에 넘길 인자 구조체
typedef struct {
    char filepath[512];
    int ttl_seconds;
} DeleteTaskArgs;

// 일정 시간 후 파일 삭제하는 스레드 함수
static void* delete_file_after_delay(void *arg) {
    DeleteTaskArgs *task = (DeleteTaskArgs *)arg;

    server_log("Timer started for file %s (ttl=%d sec)",
               task->filepath, task->ttl_seconds);

    sleep(task->ttl_seconds);

    int ret = unlink(task->filepath);
    if (ret == 0) {
        server_log("Timed-delete: removed file %s", task->filepath);
    } else {
        server_log("Timed-delete: unlink(%s) failed (errno=%d)",
                   task->filepath, errno);
    }

    free(task);
    return NULL;
}

ssize_t w;

/**
 * 파일 업로드 처리
 * MSG_FILE_UPLOAD → MSG_FILE_READY → MSG_FILE_DATA 반복 → MSG_FILE_END
 */
void handle_file_upload(int client_fd, Message *msg) {
    char filename[256];
    long filesize;
    int ttl_seconds = 0;     // 0이면 자동 삭제 없음

    // MSG_FILE_UPLOAD의 data = "filename filesize ttl"
    int parsed = sscanf(msg->data, "%s %ld %d", filename, &filesize, &ttl_seconds);
    if (parsed < 2) {
        // 형식 잘못된 경우
        Message err;
        memset(&err, 0, sizeof(err));
        err.type = MSG_ERROR;
        strcpy(err.sender, "SERVER");
        strcpy(err.data, "BAD_FILE_UPLOAD_FORMAT");
        write(client_fd, &err, sizeof(err));
        return;
    }

    server_log("File upload request: %s (%ld bytes)", filename, filesize);

    // 저장 경로 구성
    char filepath[512];
    sprintf(filepath, "%s%s", STORAGE_DIR, filename);

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        server_log("Fail File creating: %s", filepath);

        Message err;
        memset(&err, 0, sizeof(err));
        err.type = MSG_ERROR;
        strcpy(err.sender, "SERVER");
        strcpy(err.data, "FILE_OPEN_FAIL");

        w = write(client_fd, &err, sizeof(err));
        if (w < 0) perror("write");
        return;
    }

    // 🔹 1) READY 전송
    Message ready;
    memset(&ready, 0, sizeof(ready));
    ready.type = MSG_FILE_READY;
    strcpy(ready.sender, "SERVER");

    w = write(client_fd, &ready, sizeof(ready));
    if (w < 0) perror("write");

    long received = 0;

    // 🔹 2) 파일 청크 수신
    while (1) {
        Message chunk;
        int len = read(client_fd, &chunk, sizeof(chunk));
        if (len <= 0) break;

        if (chunk.type == MSG_FILE_END) {
            server_log("Sending File Upload exit signal: %s", filename);
            break;
        }

        if (chunk.type == MSG_FILE_DATA) {
            fwrite(chunk.data, 1, chunk.data_len, fp);
            received += chunk.data_len;
        }
    }

    fclose(fp);

    server_log("File Upload success %s (%ld bytes send)", filename, received);

    // 🔥 TTL 자동 삭제 스레드
    if (ttl_seconds > 0) {
        DeleteTaskArgs *task = malloc(sizeof(DeleteTaskArgs));
        if (task) {
            memset(task, 0, sizeof(*task));
            snprintf(task->filepath, sizeof(task->filepath),
                     "%s%s", STORAGE_DIR, filename);
            task->ttl_seconds = ttl_seconds;

            pthread_t tid;
            pthread_attr_t attr;

            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

            int rc = pthread_create(&tid, &attr, delete_file_after_delay, task);
            pthread_attr_destroy(&attr);

            if (rc != 0) {
                server_log("Failed to create delete timer thread for %s (rc=%d)", filename, rc);
                free(task);
            } else {
                server_log("Delete timer thread created for %s", filename);
            }

        } else {
            server_log("malloc failed for DeleteTaskArgs");
        }
    }
}


/**
 * 파일 다운로드 처리
 * MSG_FILE_DOWNLOAD → MSG_FILE_READY → MSG_FILE_DATA 반복 → MSG_FILE_END
 */
void handle_file_download(int client_fd, Message *msg) {
    char filename[256];
    strcpy(filename, msg->data);

    server_log("File Download Request: %s", filename);

    char filepath[512];
    sprintf(filepath, "%s%s", STORAGE_DIR, filename);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        server_log("There are no file in directory: %s", filename);

        Message err;
        memset(&err, 0, sizeof(err));
        err.type = MSG_ERROR;
        strcpy(err.sender, "SERVER");
        strcpy(err.data, "NOFILE");

        w = write(client_fd, &err, sizeof(err));
        if (w < 0) perror("write");

        return;
    }

    // 🔹 1) 파일 다운로드 준비됨 알림
    Message ready;
    memset(&ready, 0, sizeof(ready));
    ready.type = MSG_FILE_READY;
    strcpy(ready.sender, "SERVER");

    w = write(client_fd, &ready, sizeof(ready));
    if (w < 0) perror("write");

    // 🔹 2) 파일 청크 전송
    char buffer[MAX_BUF];
    int n;

    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        server_log("SEND DATA: %d bytes", n);

        Message chunk;
        memset(&chunk, 0, sizeof(chunk));
        chunk.type = MSG_FILE_DATA;
        strcpy(chunk.sender, "SERVER");

        memcpy(chunk.data, buffer, n);
        chunk.data_len = n;

        w = write(client_fd, &chunk, sizeof(chunk));
        if (w < 0) perror("write");
    }

    fclose(fp);

    // 🔹 3) 파일 전송 완료 메시지
    Message end;
    memset(&end, 0, sizeof(end));
    end.type = MSG_FILE_END;
    strcpy(end.sender, "SERVER");
    strcpy(end.data, filename);
    end.data_len = 0;

    w = write(client_fd, &end, sizeof(end));
    if (w < 0) perror("write");

    server_log("Success File Download: %s", filename);
}
