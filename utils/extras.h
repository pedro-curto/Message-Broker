#ifndef __UTILS_EXTRAS_H__
#define __UTILS_EXTRAS_H__

/* Auxiliar header file, with many includes that are unnecessarily repeated
 * in many .c files, along with constant definitions and structures needed in
 * more than one file
 */

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CODE_SIZE 1
#define CLIENT_PIPE_SIZE 256
#define BOX_NAME_SIZE 32
#define MESSAGE_SIZE 1024
#define ERROR_MESSAGE_SIZE 1024
#define PIPE_NAME_SIZE 256
#define PROTOCOL_SIZE 289
#define BOX_LISTING 257
#define MAX_MAILBOXES 23 // BLOCK_SIZE / sizeof(dir_entry_t)
#define BLOCK_SIZE 1024
#define MAX_BOX_SIZE 1024

typedef struct {
  uint8_t code;
  char pipename[PIPE_NAME_SIZE];
  char boxname[BOX_NAME_SIZE];
} protocol;

typedef struct {
  char box_name[BOX_NAME_SIZE];
  uint64_t box_size;
  uint64_t n_pubs;
  uint64_t n_subs;
} mail_box;

typedef struct {
  uint8_t code;
  char message[MESSAGE_SIZE];
} p_msg;

typedef struct {
  uint8_t code;
  int32_t return_code;
  char error_message[ERROR_MESSAGE_SIZE];
} box_response;

typedef struct {
  uint8_t code;
  uint8_t last;
  mail_box box;
} box_list_response;

#endif // __UTILS_EXTRAS_H__