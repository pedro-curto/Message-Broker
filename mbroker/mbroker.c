#include "extras.h"
#include "logging.h"
#include "operations.h"
#include "producer-consumer.h"

// array with information about all mailboxes
mail_box mail_boxes[MAX_MAILBOXES];
// arrays of mutexes and locks for each mailbox
pthread_mutex_t mail_locks[MAX_MAILBOXES];
pthread_cond_t mail_condvars[MAX_MAILBOXES];
// global producer-consumer queue pointer
pc_queue_t *queue;

void lock_all_boxes() {
  for (int i = 0; i < MAX_MAILBOXES; i++) {
    pthread_mutex_lock(&mail_locks[i]);
  }
}

void unlock_all_boxes() {
  for (int i = 0; i < MAX_MAILBOXES; i++) {
    pthread_mutex_unlock(&mail_locks[i]);
  }
}

// iterates through the mail_boxes array, returning the index of the box
// provided as argument, or -1 if it doesn't exist
int search_mailbox(char *box_name) {
  for (int i = 0; i < MAX_MAILBOXES; i++) {
    if (!strcmp(mail_boxes[i].box_name, box_name))
      return i;
  }
  return -1;
}

// function that handles the session of an individual publisher
int session_publisher(protocol *protocol_msg) {
  int box, pipe;
  pipe = open(protocol_msg->pipename, O_RDONLY);
  if (pipe == -1) {
    perror("write msg error");
    return -1;
  }

  int idx = search_mailbox(protocol_msg->boxname);
  if (idx != -1) {
    // box already has a publisher, ends session
    if (mail_boxes[idx].n_pubs == 1) {
      close(pipe);
      perror("box already busy");
      return -1;
    } else
      mail_boxes[idx].n_pubs = 1;
  } else { // if the box we want to link to doesn't exist, ends session
    perror("box doesn't exist");
    close(pipe);
    return -1;
  }

  box = tfs_open(protocol_msg->boxname, TFS_O_APPEND);
  if (box == -1) {
    perror("error while opening box");
    close(pipe);
    return -1;
  }
  // if an error occurred on registry, pipe is closed
  // and SIGPIPE is sent and handled on pub.c
  p_msg msg;
  // reads messages from pipe on loop and writes them to the box
  while (1) {
    if (read(pipe, &msg, sizeof(msg)) <= 0)
      break;
    if (msg.code != 9)
      break;
    // locks the corresponding box
    pthread_mutex_lock(&mail_locks[idx]);
    if (tfs_write(box, msg.message, strlen(msg.message) + 1) == -1)
      break;
    mail_boxes[idx].box_size += strlen(msg.message) + 1;
    memset(msg.message, '\0', MESSAGE_SIZE);
    pthread_mutex_unlock(&mail_locks[idx]);
    // alerts all subscribers that something has been written
    pthread_cond_broadcast(&mail_condvars[idx]);
  }
  // ends publisher session
  mail_boxes[idx].n_pubs = 0;
  close(pipe);
  tfs_close(box);
  return -1;
}

// function that handles the session of an individual subscriber
int session_subscriber(protocol *protocol_msg) {
  int box, pipe, i, j;
  char buffer[MESSAGE_SIZE];
  p_msg msg;
  msg.code = 10;
  int msg_ended = 0, can_read = 1;
  int box_id, first_read = 1;

  pipe = open(protocol_msg->pipename, O_WRONLY);
  if (pipe == -1) {
    perror("error opening communication pipe");
    return -1;
  }
  box_id = search_mailbox(protocol_msg->boxname);
  // if the box we want to subscribe doesn't exist, ends session
  if (box_id == -1) {
    close(pipe);
    // perror("no such box found");
    return -1;
  }

  // if we fail to open the box, ends the session
  box = tfs_open(protocol_msg->boxname, 0);
  if (box == -1) {
    perror("error while opening box");
    close(pipe);
    return -1;
  }

  mail_boxes[box_id].n_subs++;

  while (can_read) {
    memset(buffer, '\0', MESSAGE_SIZE);
    memset(msg.message, '\0', MESSAGE_SIZE);
    // waits on corresponding box condvar until a publisher broadcasts that
    // a change has been made
    pthread_mutex_lock(&mail_locks[box_id]);
    // doesn't want on condvar if it just entered the box: prints out the
    // previous content of the box first, and then wait on next iteration
    if (!first_read)
      pthread_cond_wait(&mail_condvars[box_id], &mail_locks[box_id]);
    first_read = 0;
    if (tfs_read(box, buffer, MAX_BOX_SIZE) == -1) {
      perror("error reading box contents");
      break;
    }
    pthread_mutex_unlock(&mail_locks[box_id]);
    j = 0;
    msg_ended = 0;
    // composes 1 or more protocol messages to send to subscriber.c
    // via the communication pipe
    while (!msg_ended) {
      memset(msg.message, '\0', MESSAGE_SIZE);
      // produces a single message
      for (i = 0; i < MESSAGE_SIZE; i++) {
        if (buffer[i] == '\0') {
          // final buffer case (two followed '\0's)
          if (buffer[i - 1] == '\0') {
            break;
          }
          msg.message[j] = '\0';
          // session final case: if the pipe is closed, stop reading
          if (write(pipe, &msg, sizeof(msg)) == -1) {
            can_read = 0;
            break;
          }
          memset(msg.message, '\0', MESSAGE_SIZE);
          j = 0;
        } else {
          msg.message[j] = buffer[i];
          j++;
        }
      }
      msg_ended = 1;
    }
  }
  mail_boxes[box_id].n_subs--;
  // end of subscriber session
  close(pipe);
  tfs_close(box);
  return 0;
}

// function that handles the request of box creation by a manager
int manager_create_box(protocol *protocol_msg) {
  box_response msg;
  msg.code = 4;
  msg.return_code = 0;
  int box, pipe;

  memset(msg.error_message, '\0', ERROR_MESSAGE_SIZE);
  pipe = open(protocol_msg->pipename, O_WRONLY);
  if (pipe == -1) {
    perror("error opening communication pipe");
    return -1;
  }

  lock_all_boxes();
  // check if box already exists
  if (search_mailbox(protocol_msg->boxname) != -1) {
    msg.return_code = -1;
    strcpy(msg.error_message, "box already exists");
  } else if ((box = tfs_open(protocol_msg->boxname, TFS_O_CREAT)) == -1) {
    msg.return_code = -1;
    perror("error creating box");
    strcpy(msg.error_message, "cannot create box");
  } else {
    tfs_close(box);
    // adds box name to mailboxes array
    for (int i = 0; i < MAX_MAILBOXES; i++) {
      if (mail_boxes[i].box_name[0] == '\0') {
        strcpy(mail_boxes[i].box_name, protocol_msg->boxname);
        break;
      }
    }
  }
  unlock_all_boxes();

  if (write(pipe, &msg, sizeof(msg)) == -1) {
    perror("error writing to communication pipe");
    return -1;
  }
  // end of session, closes pipe
  close(pipe);
  return 0;
}

// function that handles the request of box destruction by a manager
int manager_destroy_box(protocol *protocol_msg) {
  box_response msg;
  msg.code = 6;
  msg.return_code = 0;
  int pipe, box_id;
  char error_message[ERROR_MESSAGE_SIZE];
  memset(error_message, '\0', ERROR_MESSAGE_SIZE);

  pipe = open(protocol_msg->pipename, O_WRONLY);
  if (pipe == -1) {
    perror("error opening communication pipe");
    return -1;
  }

  if ((box_id = search_mailbox(protocol_msg->boxname)) == -1) {
    msg.return_code = -1;
    strcpy(msg.error_message, "box does not exist");
  } else {
    pthread_mutex_lock(&mail_locks[box_id]);
    if (tfs_unlink(protocol_msg->boxname) == -1) {
      msg.return_code = -1;
      strcpy(msg.error_message, "cannot remove box");
    }
    // no error found, regular case, resets box info from mailboxes array
    else {
      memset(mail_boxes[box_id].box_name, '\0', BOX_NAME_SIZE);
      mail_boxes[box_id].box_size = 0;
      mail_boxes[box_id].n_pubs = 0;
      mail_boxes[box_id].n_subs = 0;
    }
    pthread_mutex_unlock(&mail_locks[box_id]);
  }

  if (write(pipe, &msg, sizeof(msg)) == -1) {
    perror("error writing to communication pipe");
    return -1;
  }
  // end of session, closes pipe
  close(pipe);
  return 0;
}

// function that handles the request of box listing by a manager
int manager_list_boxes(protocol *protocol_msg) {
  int pipe, i;
  box_list_response res;
  res.code = 8;
  res.last = 0;
  pipe = open(protocol_msg->pipename, O_WRONLY);
  if (pipe == -1) {
    perror("case 8 open pipe error");
    return -1;
  }
  lock_all_boxes();
  // sends box individual info via pipe to manager
  for (i = 0; i < MAX_MAILBOXES; i++) {
    res.box = mail_boxes[i];
    if (i == MAX_MAILBOXES - 1)
      res.last = 1;
    if (write(pipe, &res, sizeof(res)) == -1) {
      perror("case 8 write pipe error");
      return -1;
    }
  }
  unlock_all_boxes();
  close(pipe);
  return 0;
}

// session function associated to every thread upon its creation
void *session() {
  uint8_t code;
  while (1) {
    // every thread awaits in pcq_dequeue on a condvar, for an enqueue to
    // be made, avoid active wait
    protocol *p = pcq_dequeue(queue);
    code = p->code;
    switch (code) {
    case 1:
      session_publisher(p);
      break;
    case 2:
      session_subscriber(p);
      break;
    case 3:
      manager_create_box(p);
      break;
    case 5:
      manager_destroy_box(p);
      break;
    case 7:
      manager_list_boxes(p);
      break;
    default:
      perror("invalid code");
    }
    free(p);
  }
}

int main(int argc, char **argv) {
  if (tfs_init(NULL) == -1) {
    perror("error initializing tfs");
    return -1;
  }
  // initializes mailboxes info array
  // and the individual mutexes/condvars arrays
  for (int i = 0; i < MAX_MAILBOXES; i++) {
    mail_boxes[i].box_name[0] = '\0';
    mail_boxes[i].box_size = 0;
    mail_boxes[i].n_pubs = 0;
    mail_boxes[i].n_subs = 0;
    pthread_mutex_init(&mail_locks[i], NULL);
    pthread_cond_init(&mail_condvars[i], NULL);
  }
  if (argc != 3) {
    perror("incorrect number of arguments");
    return -1;
  }
  char *reg_pipename = argv[1];
  int max_sessions = atoi(argv[2]);
  // arbitrary value, decided to be double of max_sessions
  size_t pcqueue_size = (size_t)max_sessions * 2;
  // if any subscriber disconnects, a SIGPIPE is sent; we ignore it
  signal(SIGPIPE, SIG_IGN);
  // unlinks any pipe that may exist with the same name before creating it
  unlink(reg_pipename);
  mkfifo(reg_pipename, 0666);
  // initializes the global queue and the threads
  queue = (pc_queue_t *)malloc(sizeof(pc_queue_t));
  pcq_create(queue, pcqueue_size);
  // creates max_sessions threads
  pthread_t sessions[max_sessions];
  for (int i = 0; i < max_sessions; i++) {
    pthread_create(&sessions[i], NULL, session, NULL);
  }
  int reg_pipe, reg_pipe_wrfd;
  // waits for register requests and handles them
  reg_pipe = open(reg_pipename, O_RDONLY);
  if (reg_pipe == -1) {
    perror("error opening register pipe");
    return -1;
  }
  // to make sure there is always at least one writer on the register pipe
  reg_pipe_wrfd = open(reg_pipename, O_WRONLY);
  (void)reg_pipe_wrfd;

  // reads and handles protocol type messages, enqueueing them
  protocol *protocol_msg;
  while (1) {
    protocol_msg = (protocol *)malloc(sizeof(protocol));
    // non-active wait because read is blocking
    if (read(reg_pipe, protocol_msg, sizeof(protocol)) == -1) {
      perror("error reading protocol");
      continue;
    }
    pcq_enqueue(queue, protocol_msg);
  }
  return 0;
}
