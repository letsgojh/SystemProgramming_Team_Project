#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "../common/protocol.h"
#include "server_user_list.h"
#include "server_auth.h"

// 외부 함수
bool check_login(const char *id, const char *pw);
void broadcast(int sender_fd, Message *msg, int max_clients);
void handle_chat_message(int client_fd, Message *msg,int max_clients);
void handle_file_upload(int client_fd, Message *msg);
void handle_file_download(int client_fd, Message *msg);
void server_log(const char *fmt, ...);
int find_client_fd(const char *name);

#define MAX_CLIENTS 10
int client_sockets[MAX_CLIENTS] = {0};

ssize_t wa;

ssize_t recv_all(int sock, void *buf, size_t size){
    size_t received = 0;
    while(received < size){
        ssize_t len = recv(sock, (char*)buf + received, size - received, 0);
        if(len <= 0) return len;
        received += len;
    }

    return received;
}

void cleanup(int signo) {
    printf("\n[SERVER] 종료 중...\n");
    server_log("서버 정상 종료됨.");
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);

    int server_fd, client_fd, max_fd, activity;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen;
    fd_set readfds;
    Message msg;

    // 업로드 파일 저장용 디렉토리
    if(system("mkdir -p server/server_storage")){
        perror("system");
    }

    // 1. 소켓 생성(IPv4, TCP로 동작하는 소켓 생성)

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // SO_REUSEADDR 설정 (서버 재시작 시 TIME_WAIT 방지)
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. 주소 지정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;           // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;   // 모든 IP에서 받기
    server_addr.sin_port = htons(SERVER_PORT);  // 포트 지정

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 3. 클라이언트 요청 대기(서버가 문열고 기다리기)
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    //printf("[DEBUG] SERVER sizeof(Message) = %ld\n", sizeof(Message));


    printf("[SERVER] Listening on port %d...\n", SERVER_PORT);
    server_log("서버 시작 (포트 %d)", SERVER_PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_fd = server_fd;

        // 기존 클라이언트 소켓들을 감시 목록에 추가
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_fd) max_fd = sd;
        }

        // 4. I/O 이벤트 감지(select(감시할 fd개수 + 1, 읽을 데이터 있는지 감시하는 파일 집합, 파일에 데이터 쓸 수 있는지 검사하기 위한 파일집합)..)
        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select error");
            continue;
        }

        // 5. 신규 접속 처리
        if (FD_ISSET(server_fd, &readfds)) {
            addrlen = sizeof(client_addr);
            client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
            if (client_fd < 0) {
                perror("accept failed");
                continue;
            }

            printf("[SERVER] 새 연결: socket %d\n", client_fd);
            server_log("클라이언트 연결 (socket %d)", client_fd);

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = client_fd;
                    break;
                }
            }
        }

        // 6. 기존 클라이언트 메시지 처리
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];

            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                //여기서 sizeof(Message)로 설정햇더라도 read로는 읽어오지 못한다.
                //int valread = read(sd, &msg, sizeof(Message)); // 클라이언트 → 서버
                int valread = recv_all(sd, &msg, sizeof(Message));
                // 연결 종료/오류
                if (valread <= 0) {
                    printf("[SERVER] Client %d disconnected\n", sd);
                    server_log("클라이언트 비정상 종료 (socket %d)", sd);
                    close(sd);
                    client_sockets[i] = 0;
                    continue;
                }

                switch (msg.type) {
                    case MSG_FILE_UPLOAD:
                        server_log("%s 파일 업로드 요청", msg.sender);
                        handle_file_upload(sd, &msg);
                        break;

                    case MSG_FILE_DOWNLOAD:
                        server_log("%s 파일 다운로드 요청", msg.sender);
                        handle_file_download(sd, &msg);
                        break;

                    case MSG_DM: {
                        int recv_fd = find_client_fd(msg.target);

                        if (recv_fd < 0) {
                            Message err;
                            memset(&err, 0, sizeof(err));

                            err.type = MSG_DM_FAIL;
                            strcpy(err.sender, "SERVER");
                            strcpy(err.data, "User not found.");
                            send(sd, &err, sizeof(err), 0);
                            break;
                        }

                        // DM 전용 메시지 재구성
                        Message dm;
                        memset(&dm, 0, sizeof(dm));

                        dm.type = MSG_DM;
                        strcpy(dm.sender, msg.sender);   // 보낸 사람
                        strcpy(dm.target, msg.target);   // 받는 사람
                        strcpy(dm.data, msg.data);       // 암호화된 본문 그대로

                        // 1) 대상자에게 전송
                        send(recv_fd, &dm, sizeof(dm), 0);

                        // 2) 보낸 사람에게도 전송
                        send(sd, &dm, sizeof(dm), 0);

                        break;
                    }
                    case MSG_CHAT:
                        if (strcmp(msg.data, "/users") == 0) {
                            send_user_list(sd);
                        }else if(msg.data[0] == '/' ){
                            handle_chat_message(sd, &msg, MAX_CLIENTS);
                        }
                        else {
                            printf("[%s]: %s\n", msg.sender, msg.data);
                            server_log("채팅: %s - %s", msg.sender, msg.data);
                            broadcast(sd, &msg, MAX_CLIENTS);
                        }
                        break;


                    case MSG_EXIT:
                        printf("[SERVER] %s exited. (socket %d)\n", msg.sender, sd);
                        server_log("클라이언트 종료: %s (socket %d)", msg.sender, sd);
                        close(sd);
                        client_sockets[i] = 0;
                        break;

                    case MSG_LOGIN:
                    {
                        char id[32], pw[32];
                        sscanf(msg.data, "%s %s", id, pw);

                        Message reply;
                        memset(&reply, 0, sizeof(reply));
                        strcpy(reply.sender, "SERVER");

                        if (check_login(id, pw)) {
                            reply.type = MSG_LOGIN_OK;
                            strcpy(reply.data, "LOGIN_OK");
                            wa = write(sd, &reply, sizeof(reply));
                            if(wa < 0){
                                perror("write");
                            }

                            register_user(sd, id);           // username 기록
                            assign_root_if_first(sd);        // root 자동 배정

                            printf("[SERVER] 로그인 성공: %s (socket %d)\n", id, sd);
                        }
                        else {
                            reply.type = MSG_LOGIN_FAIL;
                            strcpy(reply.data, "LOGIN_FAIL");
                            wa = write(sd, &reply, sizeof(reply));

                            if(wa < 0){
                                perror("write");
                            }

                            printf("[SERVER] 로그인 실패: %s\n", id);
                        }
                        break;
                    }


                    // 파일 데이터/종료 메시지는 보통 handle_file_* 내부에서 처리하겠지만,
                    // 혹시 여기로 들어오면 로그만 찍고 무시
                    case MSG_FILE_DATA:
                    case MSG_FILE_END:
                    case MSG_FILE_READY:
                    case MSG_LIST_REQEUST:
                        send_user_list(sd);
                    case MSG_ERROR:
                        server_log("예상치 못한 위치에서 파일 관련 메시지 수신(type=%d)", msg.type);
                        break;

                    default:
                        server_log("알 수 없는 메시지 타입 수신(type=%d)", msg.type);
                        break;
                }
            }
        }
    }
}
