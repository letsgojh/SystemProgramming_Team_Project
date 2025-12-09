#ifndef SERVER_AUTH_H
#define SERVER_AUTH_H

void assign_root_if_first(int client_fd);
bool can_kick(int requester_fd);
bool is_root(int client_fd);
bool transfer_root(const char *target_username);
const char* get_username(int client_fd);
void register_user(int client_fd, const char *username);
bool check_login(const char *username, const char *password);

#endif