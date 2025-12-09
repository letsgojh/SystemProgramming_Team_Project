#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "protocol.h"

extern int client_sockets[];
extern char usernames[][MAX_NAME];   // server_auth.c에서 선언된 username 테이블
extern void server_log(const char *fmt, ...);

#define MAX_CLIENTS 10

int wb;
/**
 *  접속자 목록 문자열을 생성 (username 기반)
 *  결과를 buf에 저장
 */
void build_user_list(char *buf, size_t bufsize) {
    buf[0] = '\0';  // 초기화

    char temp[128];

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] > 0 && usernames[i][0] != '\0') {
            snprintf(temp, sizeof(temp), "- %s (socket %d)\n",
                     usernames[i], client_sockets[i]);
            strncat(buf, temp, bufsize - strlen(buf) - 1);
        }
    }

    if (strlen(buf) == 0)
        strcpy(buf, "(no users online)\n");
}


/**
 *  특정 클라이언트에게 접속자 목록 전송
 */
void send_user_list(int client_fd) {
    char list_buf[1024];
    build_user_list(list_buf, sizeof(list_buf));

    Message msg;
    memset(&msg, 0, sizeof(msg));

    msg.type = MSG_CHAT;
    strcpy(msg.sender, "SERVER");
    snprintf(msg.data, MAX_BUF, "%s", list_buf);


    wb = write(client_fd, &msg, sizeof(msg));

    if(wb < 0){
        perror("write");
    }
    server_log("접속자 목록 전송 (to socket %d)", client_fd);
}

void disconnect_client(int idx) {
    if (client_sockets[idx] > 0) {
        close(client_sockets[idx]);
        client_sockets[idx] = 0;
        usernames[idx][0] = '\0';  // 이름 초기화
        printf("[SERVER] Client %d disconnected\n", idx);
    }
}

int find_client_fd(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] > 0 && strcmp(usernames[i], name) == 0) {
            return client_sockets[i];
        }
    }
    return -1;
}

