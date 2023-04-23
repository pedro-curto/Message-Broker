#include "extras.h"
#include "logging.h"

mail_box mail_boxes[MAX_MAILBOXES];
int mail_boxes_size = 0;

// compare function to provided as argument in quicksort:
// compares alphabetically
int str_compare(const void *a, const void *b) {
  mail_box *m_a = (mail_box *)a;
  mail_box *m_b = (mail_box *)b;
  return strcmp(m_a->box_name, m_b->box_name);
}

int main(int argc, char **argv) {
  if (argc != 4 && argc != 5) {
    perror("incorrect number of arguments");
    return -1;
  }
  protocol message;
  char register_pipe[PIPE_NAME_SIZE], *action = argv[3];
  memset(register_pipe, '\0', PIPE_NAME_SIZE);
  memset(message.pipename, '\0', PIPE_NAME_SIZE);
  strcpy(register_pipe, argv[1]);
  strcpy(message.pipename, argv[2]);
  // initializes mailboxes info array
  for (int i = 0; i < MAX_MAILBOXES; i++) {
    mail_boxes[i].box_name[0] = '\0';
    mail_boxes[i].box_size = 0;
    mail_boxes[i].n_pubs = 0;
    mail_boxes[i].n_subs = 0;
  }
  int regpipe_fd, pipe_fd;
  // unlinks to ensure that if a pipe with the same name exists, it is deleted
  unlink(message.pipename);
  mkfifo(message.pipename, 0666);
  regpipe_fd = open(register_pipe, O_WRONLY);
  if (regpipe_fd == -1) {
    perror("manager open");
    return -1;
  }
  // box listing
  if (!strcmp(action, "list")) {
    // listar boxes
    message.code = 7;
    if (write(regpipe_fd, &message, sizeof(protocol)) == -1) {
      perror("error writing to register pipe");
      return -1;
    }
    close(regpipe_fd);

    uint8_t last = 0;
    box_list_response res;
    size_t i = 0;
    pipe_fd = open(message.pipename, O_RDONLY);
    // reads messages from communication pipe with individual box info
    do {
      if (read(pipe_fd, &res, sizeof(res)) == -1)
        return -1;
      if (res.code != 8)
        break;

      last = res.last;
      if (res.box.box_name[0] != '\0') {
        mail_boxes[i] = res.box;
        mail_boxes_size++;
        i++;
      }
    } while (last == 0);
    close(pipe_fd);

    if (mail_boxes_size == 0) {
      fprintf(stdout, "NO BOXES FOUND\n");
      return 0;
    }
    // sorts mailboxes alphabetically
    qsort(mail_boxes, i, sizeof(mail_box), str_compare);
    // displays mailboxes info
    for (int j = 0; j < mail_boxes_size; j++) {
      fprintf(stdout, "%s %zu %zu %zu\n", mail_boxes[j].box_name,
              mail_boxes[j].box_size, mail_boxes[j].n_pubs,
              mail_boxes[j].n_subs);
    }
  }
  // box creation/deletion request
  else {
    box_response res;

    memset(message.boxname, '\0', BOX_NAME_SIZE);
    strcpy(message.boxname, argv[4]);
    if (!strcmp(action, "create"))
      message.code = 3;
    else
      message.code = 5;

    if (write(regpipe_fd, &message, sizeof(protocol)) == -1) {
      perror("error writing to register pipe");
      return -1;
    }
    close(regpipe_fd);
    // read code
    pipe_fd = open(message.pipename, O_RDONLY);

    if (read(pipe_fd, &res, sizeof(res)) == -1) {
      return -1;
    }
    if (res.code != message.code + 1) {
      perror("incorrect answer code sent");
      return -1;
    }
    // if an error occurs, displays the corresponding error message
    if (res.return_code == -1) {
      fprintf(stdout, "ERROR %s\n", res.error_message);
    }
    // otherwise, displays OK to inform that the creation/deletion succeeded
    else {
      fprintf(stdout, "OK\n");
    }
    close(pipe_fd);
    unlink(message.pipename);
  }
  return 0;
}
