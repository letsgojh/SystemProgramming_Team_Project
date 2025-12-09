#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "protocol.h"
#include <ncurses.h>
#include <sys/types.h>
#include <sys/socket.h>


// 외부 함수/변수
extern WINDOW *win_chat;
extern int sock;
extern char username[MAX_NAME];
extern void print_chat(const char *fmt, ...);

extern volatile int g_downloading;
extern FILE *g_download_fp;
extern char g_download_name[256];
extern long g_download_total;

ssize_t w;
ssize_t r;


void handle_file_data(Message *msg) {
    if (!g_downloading || g_download_fp == NULL) {
        return; // 다운로드 중이 아니면 무시
    }

    // 실제 파일 데이터 기록
    if (msg->data_len > 0) {
        size_t written = fwrite(msg->data, 1, msg->data_len, g_download_fp);
        g_download_total += written;
    }
}

void handle_file_end(Message *msg) {
    if (!g_downloading || g_download_fp == NULL) {
        return;
    }

    fclose(g_download_fp);
    g_download_fp = NULL;
    g_downloading = 0;

    print_chat("Download Success: %s (%ld bytes)", msg->data, g_download_total);
    g_download_total = 0;
}

/**
 * 파일 업로드 함수
 */
void upload_file(int sock, const char *filename, const char *username, int ttl_seconds) {

    FILE *fp = fopen(filename, "rb");
    if (!fp) { 
        print_chat("Cannot open file: %s", filename);
        return;
    }

    // 파일 크기 구하기
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 1) 업로드 요청 메시지 전송
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_FILE_UPLOAD;
    strcpy(msg.sender, username);

    // 🔥 서버가 기대하는 형식: "filename filesize ttl_seconds"
    snprintf(msg.data, sizeof(msg.data), "%s %ld %d", filename, filesize, ttl_seconds);

    w = write(sock, &msg, sizeof(msg));
    if (w < 0) {
        perror("write");
    }

    // 2) READY 메시지 대기
    Message reply;
    r = read(sock, &reply, sizeof(reply));

    if(r < 0){
        perror("read");
    }
    if (reply.type != MSG_FILE_READY) {
        print_chat("Server rejecte Upload reqeust.");
        fclose(fp);
        return;
    }

    print_chat("Upload starts: %s (%ld bytes)", filename, filesize);

    // 3) 파일 전송 (청크 기반)
    char buffer[MAX_BUF];
    long total = 0;
    int n;

    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        Message chunk;
        chunk.type = MSG_FILE_DATA;
        strcpy(chunk.sender, username);
        memcpy(chunk.data, buffer, n);
        chunk.data_len = n;

        w = write(sock, &chunk, sizeof(chunk));
        
        if(w < 0){
            perror("wirte");
        }
        total += n;
    }

    fclose(fp);

    // 4) 전송 종료 메시지
    Message end;
    end.type = MSG_FILE_END;
    strcpy(end.sender, username);
    strcpy(end.data, filename);
    end.data_len = 0;
    w = write(sock, &end, sizeof(end));

    if(w < 0){
        perror("write");
    }
    print_chat("Upload Success: %s (%ld bytes)", filename, total);
}


/**
 * 파일 다운로드 함수
 */
void download_file(int sock, const char *filename) {

    // 1) 로컬 저장 파일 열기
    char savepath[512];
    sprintf(savepath, "./client/%s", filename);
    FILE *fp = fopen(savepath, "wb");
    if (!fp) {
        print_chat("Download file create failed: %s", filename);
        return;
    }

    // 2) 다운로드 상태 설정
    g_downloading = 1;
    g_download_fp = fp;
    strcpy(g_download_name, filename);
    g_download_total = 0;

    // 3) 서버에 다운로드 요청 보내기
    Message req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_DOWNLOAD;
    strcpy(req.sender, username);
    strcpy(req.data, filename);

    ssize_t w = write(sock, &req, sizeof(req));
    if (w < 0) {
        perror("write");
        fclose(fp);
        g_downloading = 0;
        g_download_fp = NULL;
        return;
    }

    print_chat("Download Starts: %s", filename);
}
