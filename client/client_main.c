#include "../common/protocol.h"
#include "../common/encrypt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ncurses.h>
#include <ncursesw/curses.h>
#include <locale.h>
#include <signal.h>

#define MAX_DATA 1024

void upload_file(int sock, const char *filename, const char *username, int ttl_seconds);
void download_file(int sock, const char *filename);
void client_log(const char *fmt, ...);
extern void print_chat(const char *format, ...);
extern void print_chat_msg(const char *sender, const char *text);
extern void handle_chat_message(Message *msg);
extern void redraw_chat_window(void);

int sock;
char username[MAX_NAME];
int ra;

// download state
volatile int g_downloading = 0;
FILE *g_download_fp = NULL;
char g_download_name[256];
long g_download_total = 0;

// UI Windows
WINDOW *win_header = NULL;
WINDOW *win_chat   = NULL;
WINDOW *win_input  = NULL;

// resize flag (set only in signal handler)
volatile sig_atomic_t g_need_resize = 0;

// ncurses is not thread-safe, so protect UI with a mutex
pthread_mutex_t g_ui_lock = PTHREAD_MUTEX_INITIALIZER;

/* ----------------------- UI functions ----------------------- */

// create/recreate windows according to current terminal size
void create_windows(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // delete old windows if exist
    if (win_header) { delwin(win_header); win_header = NULL; }
    if (win_chat)   { delwin(win_chat);   win_chat   = NULL; }
    if (win_input)  { delwin(win_input);  win_input  = NULL; }

    // if terminal is too small, show only a warning
    if (rows < 10 || cols < 40) {
        erase();
        mvprintw(0, 0, "Terminal too small! (min 40 x 10)");
        refresh();
        return;
    }

    // Header (top)
    win_header = newwin(3, cols, 0, 0);
    box(win_header, 0, 0);
    mvwprintw(win_header, 1, 2, "Chat & File Transfer System v1.0");
    if (username[0] != '\0') {
        mvwprintw(win_header, 1, cols - (int)strlen(username) - 15,
                  "Logged in as %s", username);
    }
    wrefresh(win_header);

    // Chat window (middle)
    int chat_h = rows - 7;      // 3(header) + 4(input) = 7
    win_chat = newwin(chat_h, cols, 3, 0);
    box(win_chat, 0, 0);
    mvwprintw(win_chat, 1, 2, "CHAT AREA");
    scrollok(win_chat, TRUE);   // enable scrolling
    wrefresh(win_chat);

    // Input window (bottom)
    win_input = newwin(4, cols, rows - 4, 0);
    keypad(win_input, TRUE);            // enable keypad
    wbkgd(win_input, COLOR_PAIR(0));    // clear strange background
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "> ");
    wrefresh(win_input);
}

// ncurses init + create windows
void init_ui(void) {
    setlocale(LC_ALL,"ko_KR.utf-8");
    initscr();
    cbreak();
    noecho();
    curs_set(1);
    keypad(stdscr, TRUE);

    start_color();
    init_pair(2, COLOR_CYAN, -1);         // color for DM messages

    create_windows();
}

/* ----------------------- signal handler ----------------------- */

void handle_resize(int sig) {
    (void)sig;         // unused
    g_need_resize = 1; // real work is done in main loop
}

/* ----------------------- utility ----------------------- */

ssize_t recv_all(int sock, void *buf, size_t size) {
    size_t total = 0;

    while (total < size) {
        ssize_t len = recv(sock, (char*)buf + total, size - total, 0);
        if (len <= 0) {
            return len;
        }
        total += len;
    }
    return total;
}

/* ----------------------- recv_thread ----------------------- */

void *recv_thread(void *arg) {
    (void)arg;
    Message msg;

    while (1) {
        ssize_t len = recv_all(sock, &msg, sizeof(Message));
        if (len <= 0) {
            // server disconnected
            pthread_mutex_lock(&g_ui_lock);
            print_chat("Server disconnected");
            endwin();
            pthread_mutex_unlock(&g_ui_lock);
            exit(0);
        }

        // file download handling
        if (g_downloading && (msg.type == MSG_FILE_DATA || msg.type == MSG_FILE_END)) {

            if (msg.type == MSG_FILE_DATA && g_download_fp) {
                fwrite(msg.data, 1, msg.data_len, g_download_fp);
                g_download_total += msg.data_len;
            }

            if (msg.type == MSG_FILE_END) {
                if (g_download_fp) fclose(g_download_fp);

                pthread_mutex_lock(&g_ui_lock);
                print_chat("Download Success: %s (%ld bytes)",
                           g_download_name, g_download_total);
                pthread_mutex_unlock(&g_ui_lock);

                g_downloading    = 0;
                g_download_fp    = NULL;
                g_download_total = 0;
            }
            continue;
        }

        // chat / other messages
        if (msg.type == MSG_CHAT) {
            if (strcmp(msg.sender, username) != 0) {
                pthread_mutex_lock(&g_ui_lock);
                handle_chat_message(&msg);
                pthread_mutex_unlock(&g_ui_lock);
                flushinp();   // clear unexpected escape sequences
            }
        } else if (msg.type == MSG_KICK_NOTICE) {
            pthread_mutex_lock(&g_ui_lock);
            print_chat("[NOTICE] %s", msg.data);
            pthread_mutex_unlock(&g_ui_lock);
        }
        else if (msg.type == MSG_LOGIN_OK) {
            pthread_mutex_lock(&g_ui_lock);
            print_chat("Server: Login Success");
            pthread_mutex_unlock(&g_ui_lock);
        }
        else if (msg.type == MSG_LOGIN_FAIL) {
            pthread_mutex_lock(&g_ui_lock);
            print_chat("Server: Login Fail");
            pthread_mutex_unlock(&g_ui_lock);
        }
        else if (msg.type == MSG_LIST_RESPONSE) {
            // if server sends user list here, we can print it
            // pthread_mutex_lock(&g_ui_lock);
            // print_chat("[User List]\n%s", msg.data);
            // pthread_mutex_unlock(&g_ui_lock);
        }
        else if (msg.type == MSG_DM) {
            pthread_mutex_lock(&g_ui_lock);
            handle_chat_message(&msg);
            pthread_mutex_unlock(&g_ui_lock);
            flushinp();
        }
        else if (msg.type == MSG_DM_FAIL) {
            pthread_mutex_lock(&g_ui_lock);
            print_chat("DM failed: target user not found.");
            pthread_mutex_unlock(&g_ui_lock);
        }
        else {
            pthread_mutex_lock(&g_ui_lock);
            print_chat("Server sent message type=%d", msg.type);
            pthread_mutex_unlock(&g_ui_lock);
        }
    }

    return NULL;
}

/* ---------------- password input (**** masking) ------------- */

// read password from win_input and show only "****"
static void get_password_input(char *out, int maxlen) {
    int idx = 0;
    int ch;

    // after "PW: "
    int y = 1;
    int x = 6;   // "PW: " occupies col 2~5 → input from col 6

    // echo is already disabled (noecho())
    wmove(win_input, y, x);
    wrefresh(win_input);

    while (1) {
        ch = wgetch(win_input);

        // finish on Enter
        if (ch == '\n' || ch == '\r') {
            break;
        }

        // backspace
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (idx > 0) {
                idx--;
                // erase last '*'
                mvwaddch(win_input, y, x + idx, ' ');
                wmove(win_input, y, x + idx);
                wrefresh(win_input);
            }
            continue;
        }

        // printable characters
        if (idx < maxlen - 1 && ch >= 32 && ch <= 126) {
            out[idx++] = (char)ch;
            mvwaddch(win_input, y, x + idx - 1, '*');
            wrefresh(win_input);
        }
    }

    out[idx] = '\0';
}


/* ----------------------- main ----------------------- */

int main() {
    setlocale(LC_ALL, "");

    // handle terminal resize (SIGWINCH)
    signal(SIGWINCH, handle_resize);

    struct sockaddr_in server_addr;
    Message msg;
    pthread_t recv_tid;

    // init UI (no recv thread yet, so no lock needed)
    init_ui();

    // create socket and connect to server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        pthread_mutex_lock(&g_ui_lock);
        endwin();
        pthread_mutex_unlock(&g_ui_lock);
        perror("connect failed");
        return 1;
    }

    pthread_mutex_lock(&g_ui_lock);
    print_chat("Server Connect Success");
    pthread_mutex_unlock(&g_ui_lock);
    client_log("Server Connect Success");

    /* ---------------- Login ---------------- */

    char id[32], pw[32];

    /* ---- ID input (visible) ---- */
    pthread_mutex_lock(&g_ui_lock);
    echo();   // show ID while typing
    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "ID: ");
    wrefresh(win_input);
    pthread_mutex_unlock(&g_ui_lock);

    wgetnstr(win_input, id, sizeof(id) - 1);

    /* ---- Password input (****) ---- */
    pthread_mutex_lock(&g_ui_lock);
    noecho();   // hide actual characters
    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "PW: ");
    wrefresh(win_input);
    pthread_mutex_unlock(&g_ui_lock);

    get_password_input(pw, sizeof(pw));

    /* ---- reset input window ---- */
    pthread_mutex_lock(&g_ui_lock);
    werase(win_input);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "> ");
    wrefresh(win_input);
    pthread_mutex_unlock(&g_ui_lock);

    // send login request
    msg.type = MSG_LOGIN;
    sprintf(msg.data, "%s %s", id, pw);
    send(sock, &msg, sizeof(msg), 0);
    ra = read(sock, &msg, sizeof(msg));
    if (ra < 0) perror("read");

    if (strcmp(msg.data, "LOGIN_FAIL") == 0) {
        pthread_mutex_lock(&g_ui_lock);
        print_chat("Login Failed");
        pthread_mutex_unlock(&g_ui_lock);
        client_log("Login Failed (%s)", id);
        sleep(1);
        pthread_mutex_lock(&g_ui_lock);
        endwin();
        pthread_mutex_unlock(&g_ui_lock);
        return 0;
    }

    strcpy(username, id);
    pthread_mutex_lock(&g_ui_lock);
    print_chat("Login Success! Type /manual to see available commands.");
    pthread_mutex_unlock(&g_ui_lock);
    client_log("Login Success (%s)", username);

    // update header with username
    pthread_mutex_lock(&g_ui_lock);
    if (win_header) {
        int rows, cols;
        getmaxyx(win_header, rows, cols);
        mvwprintw(win_header, 1, cols - (int)strlen(username) - 15,
                  "Logged in as %s", username);
        wrefresh(win_header);
    }
    pthread_mutex_unlock(&g_ui_lock);

    // start receiver thread
    pthread_create(&recv_tid, NULL, recv_thread, NULL);

    /* ---------------- Main input loop ---------------- */

    char buf[MAX_BUF];

    while (1) {
        if (g_need_resize) {
            g_need_resize = 0;

            pthread_mutex_lock(&g_ui_lock);
            endwin();
            refresh();
            clear();
            init_ui();               // recreate windows

            // redraw chat history
            redraw_chat_window();

            // redraw header with username
            if (username[0] != '\0' && win_header) {
                int rows, cols;
                getmaxyx(win_header, rows, cols);
                mvwprintw(win_header, 1, cols - (int)strlen(username) - 15,
                          "Logged in as %s", username);
                wrefresh(win_header);
            }

            // reset input prompt
            werase(win_input);
            box(win_input, 0, 0);
            mvwprintw(win_input, 1, 2, "> ");
            wrefresh(win_input);
            pthread_mutex_unlock(&g_ui_lock);

            flushinp();
        }

        // reset input window for new input
        pthread_mutex_lock(&g_ui_lock);
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "> ");
        wrefresh(win_input);
        echo();
        pthread_mutex_unlock(&g_ui_lock);

        wgetnstr(win_input, buf, MAX_BUF - 1);

        pthread_mutex_lock(&g_ui_lock);
        noecho();
        pthread_mutex_unlock(&g_ui_lock);

        /* ---------- Upload ---------- */
        if (strncmp(buf, "/upload ", 8) == 0) {
            char filename[256];
            int ttl_minutes = 0;
            int count;

            count = sscanf(buf + 8, "%255s %d", filename, &ttl_minutes);
            if (count < 1) {
                pthread_mutex_lock(&g_ui_lock);
                print_chat("Usage: /upload <filename> [ttl_minutes]");
                pthread_mutex_unlock(&g_ui_lock);
                continue;
            }
            if (count == 1) ttl_minutes = 0;

            int ttl_seconds = ttl_minutes * 60;
            upload_file(sock, filename, username, ttl_seconds);

            if (ttl_minutes > 0) {
                pthread_mutex_lock(&g_ui_lock);
                print_chat("Upload request: %s (auto-delete in %d min)",
                           filename, ttl_minutes);
                pthread_mutex_unlock(&g_ui_lock);
                client_log("Upload: %s (ttl=%d min)", filename, ttl_minutes);
            } else {
                pthread_mutex_lock(&g_ui_lock);
                print_chat("Upload request: %s", filename);
                pthread_mutex_unlock(&g_ui_lock);
                client_log("Upload: %s", filename);
            }
        }

        /* ---------- Download ---------- */
        else if (strncmp(buf, "/download ", 10) == 0) {
            if (g_downloading) {
                pthread_mutex_lock(&g_ui_lock);
                print_chat("Already downloading another file!");
                pthread_mutex_unlock(&g_ui_lock);
                continue;
            }
            download_file(sock, buf + 10);
            client_log("Download request: %s", buf + 10);
        }

        /* ---------- List ---------- */
        else if (strcmp(buf, "/list") == 0) {
            Message req;
            memset(&req, 0, sizeof(req));
            req.type = MSG_LIST_REQEUST;
            strcpy(req.sender, username);
            send(sock, &req, sizeof(req), 0);
        }

        /* ---------- Exit ---------- */
        else if (strcmp(buf, "/exit") == 0) {
            msg.type = MSG_EXIT;
            strcpy(msg.sender, username);
            send(sock, &msg, sizeof(msg), 0);

            pthread_mutex_lock(&g_ui_lock);
            print_chat("Client exit");
            endwin();
            pthread_mutex_unlock(&g_ui_lock);

            client_log("Client exit");
            break;
        }

        /* ---------- Force refresh ---------- */
        else if (strcmp(buf, "/refresh") == 0) {
            pthread_mutex_lock(&g_ui_lock);
            create_windows();
            redraw_chat_window();
            pthread_mutex_unlock(&g_ui_lock);
            continue;
        }

        /* ---------- Command manual ---------- */
        else if (strcmp(buf, "/manual") == 0) {
            pthread_mutex_lock(&g_ui_lock);

            print_chat("---------- COMMAND MANUAL ----------");
            print_chat("/upload <file> [ttl_min]");
            print_chat("  - Upload a file. If ttl_min is given, the file is auto-deleted after that many minutes");
            print_chat("/download <file>");
            print_chat("  - Download a file stored on the server");
            print_chat("/list");
            print_chat("  - Show current online user list");
            print_chat("/dm <username> <message>");
            print_chat("  - Send a direct message to the target user");
            print_chat("/refresh");
            print_chat("  - Rebuild the screen layout (useful after resize glitches)");
            print_chat("/exit");
            print_chat("  - Exit the client");
            print_chat("");
            print_chat("[ROOT ONLY COMMANDS]");
            print_chat("/kick <username>");
            print_chat("  - Kick the target user from the server");
            print_chat("/root <username>");
            print_chat("  - Transfer ROOT permission to the target user");
            print_chat("------------------------------------");

            pthread_mutex_unlock(&g_ui_lock);
            continue;   // do not send anything to the server
        }

        /* ---------- DM (whisper) ---------- */
        else if (strncmp(buf, "/dm ", 4) == 0) {

            char target[MAX_NAME];
            char body[MAX_DATA];

            // /dm username message
            if (sscanf(buf + 4, "%s %[^\n]", target, body) == 2) {

                Message msg;
                memset(&msg, 0, sizeof(msg));

                msg.type = MSG_DM;
                strcpy(msg.sender, username);
                strcpy(msg.target, target);

                // encrypt DM body
                encrypt(body, msg.data);

                // send to server
                send(sock, &msg, sizeof(msg), 0);

                client_log("DM to %s: %s", target, body);
            }
            else {
                pthread_mutex_lock(&g_ui_lock);
                print_chat("Usage: /dm <username> <message>");
                pthread_mutex_unlock(&g_ui_lock);
            }

            continue;
        }

        /* ---------- normal chat ---------- */
        else {
            msg.type = MSG_CHAT;
            strcpy(msg.sender, username);
            strcpy(msg.data, buf);
            send(sock, &msg, sizeof(msg), 0);
            client_log("Chat: %s", buf);

            // also show my own message immediately
            pthread_mutex_lock(&g_ui_lock);
            handle_chat_message(&msg);
            pthread_mutex_unlock(&g_ui_lock);
        }

    }

    pthread_mutex_lock(&g_ui_lock);
    endwin();
    pthread_mutex_unlock(&g_ui_lock);

    close(sock);
    return 0;
}
