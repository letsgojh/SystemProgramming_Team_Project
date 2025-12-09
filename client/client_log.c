#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include "protocol.h"

extern char username[MAX_NAME];

// 멀티스레드 로그 보호용 mutex
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void client_log(const char *fmt, ...) {

    // 로그 디렉토리 자동 생성
    mkdir("./client", 0755);

    pthread_mutex_lock(&log_mutex);

    FILE *fp = fopen("./client/client_log.txt", "a");
    if (!fp) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    fprintf(fp, "[%02d:%02d:%02d] (%s) ",
            t->tm_hour, t->tm_min, t->tm_sec, username);

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);

    pthread_mutex_unlock(&log_mutex);
}
