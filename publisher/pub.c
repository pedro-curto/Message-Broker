#include "extras.h"
#include "logging.h"
#include <stdio.h>

int commpipe_fd;
protocol message;

// handler for SIGPIPE (end of publisher session)
void sig_handler(int signum) {
  close(commpipe_fd);
  unlink(message.pipename);
  _exit(signum);
}

int main(int argc, char **argv) {
  if (argc != 4)
    return -1;
  int regpipe_fd;

  char reg_pipename[PIPE_NAME_SIZE];
  memset(reg_pipename, '\0', PIPE_NAME_SIZE);
  memset(message.pipename, '\0', PIPE_NAME_SIZE);
  memset(message.boxname, '\0', BOX_NAME_SIZE);
  strcpy(reg_pipename, argv[1]);
  strcpy(message.pipename, argv[2]);
  strcpy(message.boxname, argv[3]);
  message.code = 1;
  // if mbroker closes communication pipe in registry, receives and handles
  // SIGPIPE
  signal(SIGPIPE, sig_handler);

  // unlinks before creating to avoid creating two pipes with the same name
  unlink(message.pipename);
  if (mkfifo(message.pipename, 0666) == -1) {
    perror("mkfifo");
    return -1;
  }
  // escrever o pedido de registo no pipe de registo do mbroker
  regpipe_fd = open(reg_pipename, O_WRONLY);
  if (regpipe_fd == -1) {
    perror("open");
    return -1;
  }
  if (write(regpipe_fd, &message, sizeof(message)) == -1) {
    perror("publisher protocol writing");
    return -1;
  }
  close(regpipe_fd);

  commpipe_fd = open(message.pipename, O_WRONLY);
  if (commpipe_fd == -1) {
    perror("opening client pipe");
    return -1;
  }
  int i, read = 1;
  char c, last_c = '\0';
  p_msg msg;
  msg.code = 9;
  // reads messages from stdin and places them in the communication pipe
  while (read) {
    memset(msg.message, '\0', MESSAGE_SIZE);
    for (i = 0; i < MESSAGE_SIZE - 1; i++) {
      c = (char)getchar();
      if (c == '\n' && last_c == '\n') {
        i--;
        continue;
      } else {
        // read '\n' character or reached message size limit
        if (c == '\n' || i == MESSAGE_SIZE - 1) {
          msg.message[i] = '\0';
          last_c = c;
          break;
        }
        // read EOF: ends publisher session
        if (c == EOF) {
          read = 0;
          break;
        }
        msg.message[i] = c;
        last_c = c;
      }
    }
    if (read) {
      if (write(commpipe_fd, &msg, sizeof(msg)) == -1) {
        perror("error while writing message to communication pipe");
        return -1;
      }
    }
  }
  close(commpipe_fd);
  return 0;
}
