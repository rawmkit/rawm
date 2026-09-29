#ifndef RAWM_CONTROL_H
#define RAWM_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <X11/Xlib.h>

enum {
  RAWM_CONTROL_VERSION = 1,
  RAWM_CONTROL_FACTOR_SCALE = 1000000
};

typedef enum {
  RAWM_CONTROL_FOCUS_STACK,
  RAWM_CONTROL_FOCUS_INDEX,
  RAWM_CONTROL_FOCUS_CLIENT,
  RAWM_CONTROL_INC_MASTER,
  RAWM_CONTROL_SET_MASTER_FACTOR,
  RAWM_CONTROL_ZOOM,
  RAWM_CONTROL_VIEW,
  RAWM_CONTROL_TOGGLE_VIEW,
  RAWM_CONTROL_CYCLE_VIEW,
  RAWM_CONTROL_KILL_CLIENT,
  RAWM_CONTROL_SET_LAYOUT,
  RAWM_CONTROL_CYCLE_LAYOUT,
  RAWM_CONTROL_TOGGLE_FLOATING,
  RAWM_CONTROL_TAG,
  RAWM_CONTROL_TOGGLE_TAG,
  RAWM_CONTROL_WINDOW_VIEW,
  RAWM_CONTROL_TOGGLE_FULLSCREEN,
  RAWM_CONTROL_FOCUS_MONITOR,
  RAWM_CONTROL_TAG_MONITOR,
  RAWM_CONTROL_RENAME_TAG,
  RAWM_CONTROL_QUIT,
  RAWM_CONTROL_COMMAND_LAST
} RawmControlCommand;

typedef enum {
  RAWM_CONTROL_MESSAGE_OTHER,
  RAWM_CONTROL_MESSAGE_INVALID,
  RAWM_CONTROL_MESSAGE_COMMAND
} RawmControlMessageResult;

typedef struct {
  RawmControlCommand command;
  int32_t arg0;
  int32_t arg1;
  Window requestor;
} RawmControlRequest;

typedef struct {
  Display *display;
  Window window;
  Atom utf8;
  Atom selection_atom;
  Atom version_atom;
  Atom supported_atom;
  Atom state_atom;
  Atom clients_atom;
  Atom feedback_atom;
  Atom command_atom;
  Atom command_text_atom;
  Atom commands[RAWM_CONTROL_COMMAND_LAST];
  unsigned char *last_state;
  size_t last_state_len;
  unsigned char *last_clients;
  size_t last_clients_len;
} RawmControl;

int rawm_control_init(RawmControl *control, Display *display, Window root,
                      int screen, Atom utf8, FILE *err);
void rawm_control_destroy(RawmControl *control);
RawmControlMessageResult rawm_control_decode(
  const RawmControl *control,
  const XClientMessageEvent *event,
  RawmControlRequest *request);
int rawm_control_publish_state(RawmControl *control,
                               const void *data, size_t length,
                               FILE *err);
int rawm_control_publish_clients(RawmControl *control,
                                 const void *data, size_t length,
                                 FILE *err);
int rawm_control_publish_feedback(RawmControl *control,
                                  const void *data, size_t length,
                                  FILE *err);
int rawm_control_read_text(RawmControl *control, Window requestor,
                           char *buffer, size_t capacity, FILE *err);

#endif
