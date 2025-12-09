#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "../common/protocol.h"
#include "server_auth.h"   // is_root, can_kick, transfer_root, get_username 등
#include "server_user_list.h"  // disconnect_client 등

extern int client_sockets[];
extern char usernames[][MAX_NAME];
extern void server_log(const char *fmt, ...);
extern void disconnect_client(int idx);   // server_main / user_list 쪽에서 구현됨

#define MAX_CLIENTS 10


/**
 *  클라이언트에게 문자열 메시지를 보내는 편의 함수
 */
void send_text(int client_fd, const char *sender, const char *text) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    
    msg.type = MSG_CHAT;

    strncpy(msg.sender, sender, sizeof(msg.sender) - 1);
    msg.sender[sizeof(msg.sender) - 1] = '\0';

    strncpy(msg.data, text, sizeof(msg.data) - 1);
    msg.data[sizeof(msg.data) - 1] = '\0';

    send(client_fd, &msg, sizeof(msg), 0);
}


/**
 *  전체 사용자에게 메시지 전송 (sender 제외)
 */
void broadcast(int sender_fd, Message *msg, int max_clients) {
    for (int i = 0; i < max_clients; i++) {
        int sd = client_sockets[i];

        if (sd > 0 && sd != sender_fd) {
            int sent = send(sd, msg, sizeof(Message), 0);

            if (sent < 0) {
                server_log("Fail Send: socket %d", sd);
                close(sd);
                client_sockets[i] = 0;
            }
        }
    }
}


/* ===================== root 권한 명령 ===================== */

/**
 * username → client_sockets[] 인덱스 찾기
 */
static int find_client_index_by_username(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] > 0 &&
            strcmp(usernames[i], name) == 0) {
            return i;
        }
    }
    return -1;
}


/**
 * root가 특정 유저 강퇴
 */
static bool kick_user_by_name(const char *target_username) {
    int idx = find_client_index_by_username(target_username);
    if (idx < 0) {
        return false;
    }

    int fd = client_sockets[idx];

    send_text(fd, "SERVER", "You have been kicked by root.");

    disconnect_client(idx);

    server_log("[SERVER] %s has been kicked.", target_username);
    return true;
}


/**
 * 슬래시(/) 명령 처리: /kick /root 등
 */
static void handle_command(int sender_fd,
                           const char *sender_name,
                           const char *text,
                           int max_clients) {

    if (!can_kick(sender_fd)) {
        send_text(sender_fd, "SERVER",
                  "\nPermission denied: root only command.");
        return;
    }

    if (strncmp(text, "/kick ", 6) == 0) {
        const char *target = text + 6;

        if (kick_user_by_name(target)) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "%s has been kicked by root.", target);

            Message msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_KICK_NOTICE;

            strncpy(msg.sender, "SERVER", sizeof(msg.sender) - 1);
            strncpy(msg.data, buf, sizeof(msg.data) - 1);

            broadcast(sender_fd, &msg, max_clients);

        } else {
            send_text(sender_fd, "SERVER", "No such user.");
        }
    }
    else if (strncmp(text, "/root ", 6) == 0) {
        const char *target = text + 6;

        if (transfer_root(target)) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "Root has been transferred to %s.", target);

            Message msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_CHAT;

            strncpy(msg.sender, "SERVER", sizeof(msg.sender) - 1);
            strncpy(msg.data, buf, sizeof(msg.data) - 1);

            broadcast(sender_fd, &msg, max_clients);
        }
        else {
            send_text(sender_fd, "SERVER",
                      "Failed to transfer root: user not found.");
        }
    }
    else {
        send_text(sender_fd, "SERVER", "Unknown command.");
    }
}


/**
 * Chat 메시지 처리:
 * - "/" 로 시작하면 명령
 * - 아니면 일반 채팅
 */
void handle_chat_message(int sender_fd, Message *msg, int max_clients) {
    const char *sender_name = get_username(sender_fd);
    if (!sender_name) sender_name = "UNKNOWN";

    if (msg->type == MSG_CHAT && msg->data[0] == '/') {
        handle_command(sender_fd, sender_name, msg->data, max_clients);
    } else {
        broadcast(sender_fd, msg, max_clients);
    }
}
