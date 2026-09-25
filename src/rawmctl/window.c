/* See LICENSE file for copyright and license details. */

#include "rawmctl.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RAWMCTL_CLIENT_LIMIT 65536L
#define RAWMCTL_TEXT_LIMIT 4096L

typedef struct
{
  Window id;
  char *class_name;
  char *title;
} RawmctlWindow;

typedef struct
{
  RawmctlWindow *items;
  size_t count;
} RawmctlWindowList;

static void
free_window_list(RawmctlWindowList *list)
{
  size_t i;

  if (!list)
    return;
  for (i = 0; i < list->count; ++i)
  {
    free(list->items[i].class_name);
    free(list->items[i].title);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0;
}

static char *
duplicate_text(const char *text)
{
  char *copy = strdup(text ? text : "");

  if (!copy)
    fprintf(stderr, "rawmctl: out of memory\n");
  return copy;
}

static void
sanitize_field(char *text)
{
  unsigned char *p = (unsigned char *)text;

  for (; *p; ++p)
  {
    if (*p == '\t' || *p == '\n' || *p == '\r' || *p < 0x20 || *p == 0x7f)
      *p = ' ';
  }
}

static char *
get_net_wm_name(Display *display, Window window)
{
  Atom utf8 = XInternAtom(display, "UTF8_STRING", True);
  Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", True);
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long count = 0, after = 0;
  unsigned char *data = NULL;
  char *result;

  if (utf8 == None || net_wm_name == None)
    return NULL;
  if (XGetWindowProperty(display, window, net_wm_name, 0, RAWMCTL_TEXT_LIMIT / 4,
                         False, utf8, &actual_type, &actual_format, &count,
                         &after, &data) != Success ||
      actual_type != utf8 || actual_format != 8 || !data)
  {
    if (data)
      XFree(data);
    return NULL;
  }

  result = malloc((size_t)count + 1);
  if (!result)
  {
    fprintf(stderr, "rawmctl: out of memory\n");
    XFree(data);
    return NULL;
  }
  memcpy(result, data, count);
  result[count] = '\0';
  XFree(data);
  sanitize_field(result);
  return result;
}

static char *
get_wm_name(Display *display, Window window)
{
  XTextProperty property = {0};
  char **list = NULL;
  char *result = NULL;
  int count = 0;

  if (!XGetWMName(display, window, &property) || !property.value)
    return duplicate_text("");

  if (XmbTextPropertyToTextList(display, &property, &list, &count) >= Success &&
      list && count > 0 && list[0])
    result = duplicate_text(list[0]);
  else
  {
    size_t length = property.nitems < RAWMCTL_TEXT_LIMIT
                  ? (size_t)property.nitems
                  : (size_t)RAWMCTL_TEXT_LIMIT;

    result = malloc(length + 1);
    if (result)
    {
      memcpy(result, property.value, length);
      result[length] = '\0';
    }
    else
      fprintf(stderr, "rawmctl: out of memory\n");
  }

  if (list)
    XFreeStringList(list);
  XFree(property.value);
  if (result)
    sanitize_field(result);
  return result;
}

static char *
get_title(Display *display, Window window)
{
  char *title = get_net_wm_name(display, window);

  if (title)
    return title;
  return get_wm_name(display, window);
}

static char *
get_class(Display *display, Window window)
{
  XClassHint hint = {0};
  char *result;

  if (!XGetClassHint(display, window, &hint))
    return duplicate_text("");

  result = duplicate_text(hint.res_class ? hint.res_class : "");
  if (hint.res_name)
    XFree(hint.res_name);
  if (hint.res_class)
    XFree(hint.res_class);
  if (result)
    sanitize_field(result);
  return result;
}

static int
load_client_ids(Display *display, Window **ids, size_t *count)
{
  Window root = DefaultRootWindow(display);
  Atom property = XInternAtom(display, "_NET_CLIENT_LIST", True);
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0, after = 0;
  unsigned char *data = NULL;
  Window *copy = NULL;
  size_t i;

  *ids = NULL;
  *count = 0;
  if (property == None)
    return 0;

  if (XGetWindowProperty(display, root, property, 0, RAWMCTL_CLIENT_LIMIT, False,
                         XA_WINDOW, &actual_type, &actual_format, &nitems,
                         &after, &data) != Success)
  {
    fprintf(stderr, "rawmctl: cannot read _NET_CLIENT_LIST\n");
    return -1;
  }
  if (actual_type == None)
  {
    if (data)
      XFree(data);
    return 0;
  }
  if (actual_type != XA_WINDOW || actual_format != 32)
  {
    fprintf(stderr, "rawmctl: invalid _NET_CLIENT_LIST property\n");
    if (data)
      XFree(data);
    return -1;
  }
  if (after != 0)
  {
    fprintf(stderr, "rawmctl: _NET_CLIENT_LIST exceeds the supported limit\n");
    if (data)
      XFree(data);
    return -1;
  }
  if (!nitems)
  {
    if (data)
      XFree(data);
    return 0;
  }

  copy = calloc((size_t)nitems, sizeof(*copy));
  if (!copy)
  {
    fprintf(stderr, "rawmctl: out of memory\n");
    XFree(data);
    return -1;
  }
  for (i = 0; i < (size_t)nitems; ++i)
    copy[i] = ((Window *)data)[i];

  XFree(data);
  *ids = copy;
  *count = (size_t)nitems;
  return 0;
}

static int
load_windows(Display *display, RawmctlWindowList *list)
{
  Window *ids = NULL;
  size_t count = 0, i;

  memset(list, 0, sizeof(*list));
  if (load_client_ids(display, &ids, &count) < 0)
    return -1;
  if (!count)
    return 0;

  list->items = calloc(count, sizeof(*list->items));
  if (!list->items)
  {
    fprintf(stderr, "rawmctl: out of memory\n");
    free(ids);
    return -1;
  }

  for (i = 0; i < count; ++i)
  {
    XWindowAttributes attributes;

    if (!XGetWindowAttributes(display, ids[i], &attributes))
      continue;

    list->items[list->count].id = ids[i];
    list->items[list->count].class_name = get_class(display, ids[i]);
    list->items[list->count].title = get_title(display, ids[i]);
    if (!list->items[list->count].class_name ||
        !list->items[list->count].title)
    {
      free(ids);
      free_window_list(list);
      return -1;
    }
    ++list->count;
  }

  free(ids);
  return 0;
}

static int
parse_window_id(const char *text, Window *window, const char **end_out)
{
  char *end = NULL;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 0);
  if (errno || !end || end == text || value == 0)
    return -1;
  if (end_out)
    *end_out = end;
  else if (*end)
    return -1;

  *window = (Window)value;
  return 0;
}

static int
window_in_list(const RawmctlWindowList *list, Window window)
{
  size_t i;

  for (i = 0; i < list->count; ++i)
  {
    if (list->items[i].id == window)
      return 1;
  }
  return 0;
}

static int
activate_window(Display *display, const RawmctlWindowList *list, Window window)
{
  XEvent event = {0};
  Window root = DefaultRootWindow(display);
  Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

  if (!window_in_list(list, window))
  {
    fprintf(stderr, "rawmctl: window 0x%lx is not managed\n", window);
    return EXIT_FAILURE;
  }

  event.xclient.type = ClientMessage;
  event.xclient.window = window;
  event.xclient.message_type = active;
  event.xclient.format = 32;
  event.xclient.data.l[0] = 2; /* EWMH source indication: pager/control tool. */
  event.xclient.data.l[1] = CurrentTime;
  event.xclient.data.l[2] = None;

  if (!XSendEvent(display, root, False,
                  SubstructureRedirectMask | SubstructureNotifyMask, &event))
  {
    fprintf(stderr, "rawmctl: cannot request activation of 0x%lx\n", window);
    return EXIT_FAILURE;
  }
  XFlush(display);
  return EXIT_SUCCESS;
}

int
rawmctl_window_list(Display *display)
{
  RawmctlWindowList list;
  size_t i;

  if (load_windows(display, &list) < 0)
    return EXIT_FAILURE;

  for (i = 0; i < list.count; ++i)
    printf("0x%lx\t%s\t%s\n", list.items[i].id,
           list.items[i].class_name, list.items[i].title);

  free_window_list(&list);
  return EXIT_SUCCESS;
}

int
rawmctl_window_activate(Display *display, const char *window_text)
{
  RawmctlWindowList list;
  Window window;
  int result;

  if (parse_window_id(window_text, &window, NULL) < 0)
  {
    fprintf(stderr, "rawmctl: invalid window id: %s\n", window_text);
    return EXIT_FAILURE;
  }
  if (load_windows(display, &list) < 0)
    return EXIT_FAILURE;

  result = activate_window(display, &list, window);
  free_window_list(&list);
  return result;
}

static int
write_menu(FILE *menu, const RawmctlWindowList *list)
{
  size_t i;

  for (i = 0; i < list->count; ++i)
  {
    if (fprintf(menu, "0x%lx  %s - %s\n", list->items[i].id,
                list->items[i].class_name, list->items[i].title) < 0)
      return -1;
  }
  return fflush(menu) == 0 ? 0 : -1;
}

static int
run_rawmenu(FILE *menu, char **selection, int *child_status)
{
  int output_pipe[2];
  pid_t pid;
  FILE *output = NULL;
  char *line = NULL;
  size_t capacity = 0;
  ssize_t length;
  int status;

  *selection = NULL;
  *child_status = -1;
  if (pipe(output_pipe) < 0)
  {
    fprintf(stderr, "rawmctl: pipe: %s\n", strerror(errno));
    return -1;
  }

  pid = fork();
  if (pid < 0)
  {
    fprintf(stderr, "rawmctl: fork: %s\n", strerror(errno));
    close(output_pipe[0]);
    close(output_pipe[1]);
    return -1;
  }
  if (pid == 0)
  {
    if (dup2(fileno(menu), STDIN_FILENO) < 0 ||
        dup2(output_pipe[1], STDOUT_FILENO) < 0)
    {
      fprintf(stderr, "rawmctl: dup2: %s\n", strerror(errno));
      _exit(126);
    }
    close(output_pipe[0]);
    close(output_pipe[1]);
    execlp("rawmenu", "rawmenu", "-M", "select", "-p", "Window:",
           (char *)NULL);
    fprintf(stderr, "rawmctl: cannot execute rawmenu: %s\n", strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
  }

  close(output_pipe[1]);
  output = fdopen(output_pipe[0], "r");
  if (!output)
  {
    fprintf(stderr, "rawmctl: fdopen: %s\n", strerror(errno));
    close(output_pipe[0]);
    (void)waitpid(pid, &status, 0);
    return -1;
  }

  length = getline(&line, &capacity, output);
  fclose(output);
  if (waitpid(pid, &status, 0) < 0)
  {
    fprintf(stderr, "rawmctl: waitpid: %s\n", strerror(errno));
    free(line);
    return -1;
  }

  *child_status = status;
  if (length > 0)
  {
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
      line[--length] = '\0';
    *selection = line;
  }
  else
    free(line);
  return 0;
}

int
rawmctl_window_select(Display *display)
{
  RawmctlWindowList list;
  FILE *menu = NULL;
  char *selection = NULL;
  const char *end = NULL;
  Window window;
  int child_status, result = EXIT_FAILURE;

  if (load_windows(display, &list) < 0)
    return EXIT_FAILURE;
  if (!list.count)
  {
    fprintf(stderr, "rawmctl: no managed windows\n");
    free_window_list(&list);
    return EXIT_FAILURE;
  }

  menu = tmpfile();
  if (!menu)
  {
    fprintf(stderr, "rawmctl: tmpfile: %s\n", strerror(errno));
    free_window_list(&list);
    return EXIT_FAILURE;
  }
  if (write_menu(menu, &list) < 0 || fseek(menu, 0, SEEK_SET) < 0)
  {
    fprintf(stderr, "rawmctl: cannot prepare rawmenu input\n");
    fclose(menu);
    free_window_list(&list);
    return EXIT_FAILURE;
  }

  if (run_rawmenu(menu, &selection, &child_status) < 0)
    goto out;
  if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
    goto out;
  if (!selection || parse_window_id(selection, &window, &end) < 0 ||
      (*end != '\0' && *end != ' ' && *end != '\t'))
  {
    fprintf(stderr, "rawmctl: rawmenu returned an invalid window selection\n");
    goto out;
  }

  result = activate_window(display, &list, window);

out:
  free(selection);
  fclose(menu);
  free_window_list(&list);
  return result;
}
