#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include "protocol.h"


extern int client_sockets[];
#include <sys/socket.h>   // send() 사용용
#include <unistd.h>       

// 최대 10명 사용자 이름 저장
char usernames[MAX_CLIENTS][MAX_NAME] = {0};

// root 사용자 socket_fd 저장 (-1이면 없음)
static int root_fd = -1;


/**
 * users.txt 에서 ID/PW 인증
 */
bool check_login(const char *id, const char *pw) {

    FILE *fp = fopen("./users.txt", "r");  // 실행 경로 무관하게
    if (!fp) {
        perror("users.txt open failed");
        return false;
    }

    char fid[32], fpw[32];

    while (fscanf(fp, "%s %s", fid, fpw) == 2) {
        if (strcmp(fid, id) == 0 && strcmp(fpw, pw) == 0) {
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}
/**
 * 로그인 성공한 유저 → socket_fd 에 username 저장
 */
void register_user(int socket_fd, const char *username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == socket_fd) {
            strncpy(usernames[i], username, MAX_NAME - 1);
            break;
        }
    }
}
/**
 * 서버에서 현재 유저의 username 얻기
 */
const char* get_username(int socket_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == socket_fd) {
            return usernames[i];
        }
    }
    return NULL;
}

/**
 * root 권한 배정 (가장 먼저 로그인한 사용자)
 */
void assign_root_if_first(int socket_fd) {
    if (root_fd == -1) {
        root_fd = socket_fd;

        // root된 사용자에게만 공지 보내기
        Message msg;
        memset(&msg, 0, sizeof(msg));

        msg.type = MSG_CHAT;
        strcpy(msg.sender, "SERVER");
        strcpy(msg.data, "You are now ROOT user. You can use /kick and /root.");

        send(socket_fd, &msg, sizeof(msg), 0);
    }
}


/**
 * root 여부 확인
 */
bool is_root(int socket_fd) {
    return socket_fd == root_fd;
}


/**
 * root 권한 양도
 * "/root user2" 같은 커맨드 처리용 (원하면 server_chat에서 연동)
 */
bool transfer_root(const char *target_username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(usernames[i], target_username) == 0) {
            root_fd = client_sockets[i];
            printf("[SERVER] 🔑 Root permission transferred to %s\n", target_username);
            return true;
        }
    }
    return false;
}
bool can_kick(int requester_fd) {
    // 지금 구조에서는 root만 kick 가능하게
    return is_root(requester_fd);
}

