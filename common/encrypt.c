#include "./protocol.h"
#include <string.h>

void encrypt(const char *in, char *out) {
    unsigned char key = 0x5A;  // 원하는 XOR 키
    int i;
    for (i = 0; in[i]; i++) {
        out[i] = in[i] ^ key;
    }
    out[i] = '\0';
}

void decrypt(const char *in, char *out) {
    encrypt(in, out); // XOR은 양방향 동일
}