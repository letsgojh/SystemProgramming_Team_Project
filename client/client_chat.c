#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "../common/protocol.h"

extern WINDOW *win_chat;
extern int  sock;
extern char username[MAX_NAME];

#define MAX_HISTORY 1000

typedef struct {
    char text[1024];
    int  right_align;   // 0=왼쪽, 1=오른쪽
    int  color_pair;    // 0=기본, 2=DM
} ChatLine;

static ChatLine chat_history[MAX_HISTORY];
static int      chat_history_count = 0;

static int chat_cur_line = 1;

/* ----------------------------- */
/*  히스토리에 저장              */
/* ----------------------------- */
static void push_history(const char *text, int right_align, int color_pair)
{
    if (chat_history_count >= MAX_HISTORY) {
        memmove(&chat_history[0], &chat_history[1],
                sizeof(ChatLine) * (MAX_HISTORY - 1));
        chat_history_count = MAX_HISTORY - 1;
    }

    strncpy(chat_history[chat_history_count].text,
            text,
            sizeof(chat_history[chat_history_count].text) - 1);
    chat_history[chat_history_count].text[
        sizeof(chat_history[chat_history_count].text) - 1] = '\0';

    chat_history[chat_history_count].right_align = right_align;
    chat_history[chat_history_count].color_pair  = color_pair;

    chat_history_count++;
}

/* ----------------------------- */
/*  실제 화면 출력               */
/* ----------------------------- */
static void add_chat_line(const char *text, int right_align, int color_pair)
{
    if (!win_chat) return;

    int maxy, maxx;
    getmaxyx(win_chat, maxy, maxx);

    int inner_height = maxy - 2;
    int inner_width  = maxx - 2;

    const char *p = text;

    while (*p) {
        // 스크롤 처리
        if (chat_cur_line > inner_height) {
            wscrl(win_chat, 1);
            chat_cur_line = inner_height;
        }

        // 이번 줄에 찍을 글자 수
        int remain = strlen(p);
        int len = (remain > inner_width) ? inner_width : remain;

        int start_col = 1;
        if (right_align && len < inner_width)
            start_col = 1 + (inner_width - len);

        // 색 적용
        if (color_pair > 0)
            wattron(win_chat, COLOR_PAIR(color_pair));

        // 한 줄 지우고 새로 쓰기
        mvwhline(win_chat, chat_cur_line, 1, ' ', inner_width);
        mvwprintw(win_chat, chat_cur_line, start_col, "%.*s", len, p);

        if (color_pair > 0)
            wattroff(win_chat, COLOR_PAIR(color_pair));

        chat_cur_line++;
        p += len;   // 다음 조각으로 이동
    }

    box(win_chat, 0, 0);
    wrefresh(win_chat);
}

/* ----------------------------- */
/*  시스템 메시지 출력           */
/* ----------------------------- */
void print_chat(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    push_history(buf, 0, 0);
    add_chat_line(buf, 0, 0);
}

/* ----------------------------- */
/*  일반 채팅 메시지 출력        */
/* ----------------------------- */
void print_chat_msg(const char *sender, const char *text)
{
    char line[1024];
    snprintf(line, sizeof(line), "[%s] %s", sender, text);

    int is_self = (strcmp(sender, username) == 0);

    push_history(line, is_self, 0);
    add_chat_line(line, is_self, 0);
}

/* ----------------------------- */
/*  화면 리프레시(색상 포함)     */
/* ----------------------------- */
void redraw_chat_window(void)
{
    if (!win_chat) return;

    int maxy, maxx;
    getmaxyx(win_chat, maxy, maxx);

    int inner_height = maxy - 2;

    werase(win_chat);
    box(win_chat, 0, 0);
    mvwprintw(win_chat, 1, 2, "CHAT AREA");

    chat_cur_line = 2;

    int start = 0;
    int available = inner_height - 1;

    if (chat_history_count > available)
        start = chat_history_count - available;

    for (int i = start; i < chat_history_count; i++) {
        add_chat_line(chat_history[i].text,
                      chat_history[i].right_align,
                      chat_history[i].color_pair);
    }
}

/* ----------------------------- */
/*  서버로 메시지 보냄           */
/* ----------------------------- */
void send_chat_message(const char *msg_text)
{
    Message msg;
    memset(&msg, 0, sizeof(msg));

    msg.type = MSG_CHAT;
    strcpy(msg.sender, username);
    strcpy(msg.data, msg_text);

    send(sock, &msg, sizeof(msg), 0);
}

/* ----------------------------- */
/*  서버에서 메시지 수신 처리    */
/* ----------------------------- */
void handle_chat_message(Message *msg)
{
    /* ---------------- DM 메시지 ---------------- */
    if (msg->type == MSG_DM) {

        char decrypted[MAX_BUF];
        decrypt(msg->data, decrypted);

        // decrypt 후 줄바꿈 제거
        for (int i = 0; decrypted[i]; i++) {
            unsigned char c = (unsigned char)decrypted[i];
            if (c < 32 || c == 127) {   // 보이는 문자(스페이스~틸드)만 남김
                decrypted[i] = ' ';
            }
        }


        char line[1024];
        snprintf(line, sizeof(line),
                 "[DM] %s to %s: %s",
                 msg->sender, msg->target, decrypted);

        int is_self = (strcmp(msg->sender, username) == 0);

        push_history(line, is_self, 2);      // DM은 색상 2번
        add_chat_line(line, is_self, 2);

        return;
    }

    /* ---------------- 일반 메시지 ---------------- */
    print_chat_msg(msg->sender, msg->data);
    flushinp();   // ← DM 출력 중 curses가 만든 escape code 즉시 제거
}
