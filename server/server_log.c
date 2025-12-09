#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <pthread.h>

pthread_mutex_t server_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * 서버 로그 기록 (멀티클라이언트/멀티쓰레드 안전)
 */
void server_log(const char *fmt, ...) {
    // server 디렉토리 자동 생성
    mkdir("./server", 0755);

    pthread_mutex_lock(&server_log_mutex);

    FILE *fp = fopen("./server/server_log.txt", "a");
    if (!fp) {
        pthread_mutex_unlock(&server_log_mutex);
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);

    pthread_mutex_unlock(&server_log_mutex);
}
