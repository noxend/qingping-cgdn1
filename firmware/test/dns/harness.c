// A DNS server on 127.0.0.1 that answers with dns_answer.c, for run.sh to query with
// dig, or one packet through it:
//   serve <port> <packets>   answer that many packets with 10.47.0.1, then exit
//   once <query-hex>         print the reply in hex, "none" when there is no reply
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "dns_answer.h"

static int unhex(const char *s, uint8_t *out, int cap) {
  int n = 0;
  for (; s[0] && s[1] && n < cap; s += 2) {
    char b[3] = {s[0], s[1], 0};
    out[n++] = (uint8_t) strtoul(b, NULL, 16);
  }
  return n;
}

int main(int argc, char **argv) {
  uint32_t ip = inet_addr("10.47.0.1");
  uint8_t q[600];
  uint8_t r[600];
  if (argc >= 3 && strcmp(argv[1], "once") == 0) {
    int len = unhex(argv[2], q, sizeof(q));
    // An exact-size heap copy, so the address sanitizer sees a read past the packet.
    uint8_t *copy = malloc(len ? len : 1);
    memcpy(copy, q, len);
    int n = dns_answer(copy, len, ip, r, sizeof(r));
    free(copy);
    if (n == 0) {
      printf("none\n");
    }
    for (int i = 0; i < n; i++) {
      printf("%02x", r[i]);
    }
    printf(n ? "\n" : "");
    return 0;
  }
  if (argc >= 4 && strcmp(argv[1], "serve") == 0) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(atoi(argv[2]))};
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(s, (struct sockaddr *) &a, sizeof(a)) != 0) {
      perror("bind");
      return 1;
    }
    printf("ready\n");
    fflush(stdout);
    for (int left = atoi(argv[3]); left > 0; left--) {
      struct sockaddr_in from;
      socklen_t fl = sizeof(from);
      int len = (int) recvfrom(s, q, sizeof(q), 0, (struct sockaddr *) &from, &fl);
      int n = dns_answer(q, len, ip, r, sizeof(r));
      if (n > 0) {
        sendto(s, r, n, 0, (struct sockaddr *) &from, fl);
      }
    }
    close(s);
    return 0;
  }
  return 2;
}
