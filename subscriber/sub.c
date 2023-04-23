#include "extras.h"
#include "logging.h"

protocol message;
int msg_num = 0;
int pipe_num;

// handles sigpipes received from mbroker (registry failed)
void sigpipe_handler(int signum) {
  unlink(message.pipename);
  _exit(signum);
}

// handles sigint received from user (session end)
void sigint_handler(int signum) {
  char buffer[25];
  snprintf(buffer, 25, "messages received: %d\n", msg_num);
  if (write(STDOUT_FILENO, buffer, strlen(buffer)) == -1)
    return;
  unlink(message.pipename);
  _exit(signum);
}

int main(int argc, char **argv) {
  signal(SIGINT, sigint_handler);
  signal(SIGPIPE, sigpipe_handler);

  if (argc != 4)
    return -1;
  // buffer initializations and copies from argvs to compose protocol
  int pipen;
  char reg_pipename[PIPE_NAME_SIZE];
  memset(reg_pipename, '\0', PIPE_NAME_SIZE);
  memset(message.pipename, '\0', PIPE_NAME_SIZE);
  memset(message.boxname, '\0', BOX_NAME_SIZE);
  strcpy(reg_pipename, argv[1]);
  strcpy(message.pipename, argv[2]);
  strcpy(message.boxname, argv[3]);
  message.code = 2;

  unlink(message.pipename);
  mkfifo(message.pipename, 0666);

  pipen = open(reg_pipename, O_WRONLY);
  if (pipen == -1) {
    perror("opening reg pipe");
    return -1;
  }
  // sends register request to mbroker
  if (write(pipen, &message, sizeof(protocol)) == -1) {
    perror("sub protocol writing");
    return -1;
  }

  pipe_num = open(message.pipename, O_RDONLY);
  if (pipe_num == -1) {
    perror("opening client pipe");
    return -1;
  }

  int n;
  p_msg msg;
  // reads messages sent to communication pipe, and prints them on stdout
  while ((n = (int)read(pipe_num, &msg, sizeof(msg))) > 0) {
    if (msg.code != 10)
      break;
    msg_num++;
    fprintf(stdout, "%s\n", msg.message);
    memset(msg.message, '\0', MESSAGE_SIZE);
  }
  // not supposed to reach this return, because the previous while only
  // ends with a SIGINT
  return -1;
}