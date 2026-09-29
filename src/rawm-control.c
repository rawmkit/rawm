/* See LICENSE file for copyright and license details. */

#include "rawm-control.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>

static const char *const command_names[RAWM_CONTROL_COMMAND_LAST] = {
  [RAWM_CONTROL_FOCUS_STACK] = "_RAWM_CMD_FOCUS_STACK",
  [RAWM_CONTROL_FOCUS_INDEX] = "_RAWM_CMD_FOCUS_INDEX",
  [RAWM_CONTROL_INC_MASTER] = "_RAWM_CMD_INC_MASTER",
  [RAWM_CONTROL_SET_MASTER_FACTOR] = "_RAWM_CMD_SET_MASTER_FACTOR",
  [RAWM_CONTROL_ZOOM] = "_RAWM_CMD_ZOOM",
  [RAWM_CONTROL_VIEW] = "_RAWM_CMD_VIEW",
  [RAWM_CONTROL_TOGGLE_VIEW] = "_RAWM_CMD_TOGGLE_VIEW",
  [RAWM_CONTROL_CYCLE_VIEW] = "_RAWM_CMD_CYCLE_VIEW",
  [RAWM_CONTROL_KILL_CLIENT] = "_RAWM_CMD_KILL_CLIENT",
  [RAWM_CONTROL_SET_LAYOUT] = "_RAWM_CMD_SET_LAYOUT",
  [RAWM_CONTROL_CYCLE_LAYOUT] = "_RAWM_CMD_CYCLE_LAYOUT",
  [RAWM_CONTROL_TOGGLE_FLOATING] = "_RAWM_CMD_TOGGLE_FLOATING",
  [RAWM_CONTROL_TAG] = "_RAWM_CMD_TAG",
  [RAWM_CONTROL_TOGGLE_TAG] = "_RAWM_CMD_TOGGLE_TAG",
  [RAWM_CONTROL_WINDOW_VIEW] = "_RAWM_CMD_WINDOW_VIEW",
  [RAWM_CONTROL_TOGGLE_FULLSCREEN] = "_RAWM_CMD_TOGGLE_FULLSCREEN",
  [RAWM_CONTROL_FOCUS_MONITOR] = "_RAWM_CMD_FOCUS_MONITOR",
  [RAWM_CONTROL_TAG_MONITOR] = "_RAWM_CMD_TAG_MONITOR",
  [RAWM_CONTROL_RENAME_TAG] = "_RAWM_CMD_RENAME_TAG",
  [RAWM_CONTROL_QUIT] = "_RAWM_CMD_QUIT",
};

static int32_t
card32_to_i32(long value)
{
  return (int32_t)(uint32_t)value;
}

int
rawm_control_init(RawmControl *control, Display *display, Window root,
                  int screen, Atom utf8, FILE *err)
{
  XSetWindowAttributes attributes;
  unsigned long version = RAWM_CONTROL_VERSION;
  char selection_name[64];
  int selection_length;
  size_t i;

  if (!control || !display || root == None || screen < 0 || utf8 == None)
    return -1;

  memset(control, 0, sizeof(*control));
  control->display = display;
  control->utf8 = utf8;

  selection_length = snprintf(selection_name, sizeof(selection_name),
                              "_RAWM_CONTROL_S%d", screen);
  if (selection_length < 0 ||
      (size_t)selection_length >= sizeof(selection_name))
  {
    if (err)
      fputs("rawm: control selection name is too long\n", err);
    return -1;
  }

  control->selection_atom = XInternAtom(display, selection_name, False);
  control->version_atom = XInternAtom(display, "_RAWM_CONTROL_VERSION", False);
  control->supported_atom = XInternAtom(display, "_RAWM_SUPPORTED_COMMANDS", False);
  control->state_atom = XInternAtom(display, "_RAWM_STATE", False);
  control->command_atom = XInternAtom(display, "_RAWM_COMMAND", False);
  control->command_text_atom = XInternAtom(display, "_RAWM_COMMAND_TEXT", False);

  if (control->selection_atom == None || control->version_atom == None ||
      control->supported_atom == None || control->state_atom == None ||
      control->command_atom == None || control->command_text_atom == None)
  {
    if (err)
      fputs("rawm: could not intern control-plane atoms\n", err);
    return -1;
  }

  for (i = 0; i < RAWM_CONTROL_COMMAND_LAST; ++i)
  {
    control->commands[i] = XInternAtom(display, command_names[i], False);
    if (control->commands[i] == None)
    {
      if (err)
        fprintf(err, "rawm: could not intern control command atom '%s'\n",
                command_names[i]);
      return -1;
    }
  }

  memset(&attributes, 0, sizeof(attributes));
  attributes.override_redirect = True;
  control->window = XCreateWindow(display, root, -1, -1, 1, 1, 0, 0,
                                  InputOnly, CopyFromParent,
                                  CWOverrideRedirect, &attributes);
  if (control->window == None)
  {
    if (err)
      fputs("rawm: could not create control owner window\n", err);
    return -1;
  }

  XSetSelectionOwner(display, control->selection_atom,
                     control->window, CurrentTime);
  XSync(display, False);
  if (XGetSelectionOwner(display, control->selection_atom) != control->window)
  {
    if (err)
      fputs("rawm: could not acquire control selection\n", err);
    XDestroyWindow(display, control->window);
    control->window = None;
    return -1;
  }

  XChangeProperty(display, control->window, control->version_atom,
                  XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&version, 1);
  XChangeProperty(display, control->window, control->supported_atom,
                  XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)control->commands,
                  RAWM_CONTROL_COMMAND_LAST);
  return 0;
}

void
rawm_control_destroy(RawmControl *control)
{
  if (!control)
    return;

  if (control->display && control->window != None)
    XDestroyWindow(control->display, control->window);

  free(control->last_state);
  memset(control, 0, sizeof(*control));
}

RawmControlMessageResult
rawm_control_decode(const RawmControl *control,
                    const XClientMessageEvent *event,
                    RawmControlRequest *request)
{
  Atom opcode;
  size_t i;

  if (!control || !event || event->message_type != control->command_atom)
    return RAWM_CONTROL_MESSAGE_OTHER;

  if (event->window != control->window || event->format != 32 ||
      (uint32_t)event->data.l[0] != RAWM_CONTROL_VERSION)
    return RAWM_CONTROL_MESSAGE_INVALID;

  opcode = (Atom)(uint32_t)event->data.l[1];
  for (i = 0; i < RAWM_CONTROL_COMMAND_LAST; ++i)
  {
    if (opcode != control->commands[i])
      continue;

    if (request)
    {
      request->command = (RawmControlCommand)i;
      request->arg0 = card32_to_i32(event->data.l[2]);
      request->arg1 = card32_to_i32(event->data.l[3]);
      request->requestor = (Window)(uint32_t)event->data.l[4];
    }
    return RAWM_CONTROL_MESSAGE_COMMAND;
  }

  return RAWM_CONTROL_MESSAGE_INVALID;
}

int
rawm_control_publish_state(RawmControl *control,
                           const void *data, size_t length,
                           FILE *err)
{
  unsigned char *copy = NULL;

  if (!control || !control->display || control->window == None ||
      control->state_atom == None || (!data && length))
    return -1;
  if (length > INT_MAX)
  {
    if (err)
      fputs("rawm: control state exceeds X11 property size limit\n", err);
    return -1;
  }

  if (control->last_state_len == length &&
      (!length || !memcmp(control->last_state, data, length)))
    return 0;

  if (length)
  {
    copy = malloc(length);
    if (!copy)
    {
      if (err)
        fputs("rawm: out of memory while publishing control state\n", err);
      return -1;
    }
    memcpy(copy, data, length);
  }

  XChangeProperty(control->display, control->window, control->state_atom,
                  control->utf8, 8, PropModeReplace,
                  (const unsigned char *)data, (int)length);
  XFlush(control->display);
  free(control->last_state);
  control->last_state = copy;
  control->last_state_len = length;
  return 1;
}

int
rawm_control_read_text(RawmControl *control, Window requestor,
                       char *buffer, size_t capacity, FILE *err)
{
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0, bytes_after = 0;
  unsigned char *property = NULL;
  unsigned long long_length;
  int status;

  if (!control || requestor == None || !buffer || capacity == 0)
    return -1;

  long_length = (unsigned long)((capacity + 3u) / 4u);
  status = XGetWindowProperty(control->display, requestor,
                              control->command_text_atom,
                              0, (long)long_length, True,
                              control->utf8, &actual_type, &actual_format,
                              &nitems, &bytes_after, &property);
  if (status != Success || actual_type != control->utf8 ||
      actual_format != 8 || bytes_after != 0 || nitems >= capacity)
  {
    if (err)
      fputs("rawm: invalid external command text payload\n", err);
    if (property)
      XFree(property);
    return -1;
  }

  if (nitems && memchr(property, '\0', nitems))
  {
    if (err)
      fputs("rawm: external command text contains NUL\n", err);
    XFree(property);
    return -1;
  }

  if (nitems)
    memcpy(buffer, property, nitems);
  buffer[nitems] = '\0';
  if (property)
    XFree(property);
  return (int)nitems;
}
