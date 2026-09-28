#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void
pause_for_wm(void)
{
  struct timespec ts = { 0, 120000000L };
  nanosleep(&ts, NULL);
}

static Window
find_bar(Display *dpy, Window root)
{
  Window root_return, parent_return, *children = NULL;
  unsigned int count = 0, i;
  int screen_width = DisplayWidth(dpy, DefaultScreen(dpy));
  Window bar = None;

  if (!XQueryTree(dpy, root, &root_return, &parent_return, &children, &count))
    return None;
  for (i = 0; i < count; ++i) {
    XWindowAttributes attr;

    if (!XGetWindowAttributes(dpy, children[i], &attr))
      continue;
    if (attr.map_state == IsViewable && attr.width == screen_width &&
        attr.height > 0 && attr.height < 128) {
      bar = children[i];
      break;
    }
  }
  if (children)
    XFree(children);
  return bar;
}

static uint64_t
checksum_window(Display *dpy, Window window)
{
  XWindowAttributes attr;
  XImage *image;
  uint64_t hash = UINT64_C(1469598103934665603);
  int x, y;

  if (!XGetWindowAttributes(dpy, window, &attr))
    return 0;
  image = XGetImage(dpy, window, 0, 0, (unsigned int)attr.width,
                    (unsigned int)attr.height, AllPlanes, ZPixmap);
  if (!image)
    return 0;
  for (y = 0; y < attr.height; ++y) {
    for (x = 0; x < attr.width; ++x) {
      hash ^= (uint64_t)XGetPixel(image, x, y);
      hash *= UINT64_C(1099511628211);
    }
  }
  XDestroyImage(image);
  return hash;
}

static void
set_status(Display *dpy, Window root, Atom property, Atom type,
           const unsigned char *text, size_t length)
{
  XChangeProperty(dpy, root, property, type, 8, PropModeReplace, text,
                  (int)length);
  XFlush(dpy);
  pause_for_wm();
}

static int
click_status(Display *dpy, Window bar, Window root)
{
  XWindowAttributes attr;
  XEvent event;

  if (!XGetWindowAttributes(dpy, bar, &attr))
    return 0;

  memset(&event, 0, sizeof(event));
  event.xbutton.type = ButtonPress;
  event.xbutton.display = dpy;
  event.xbutton.window = bar;
  event.xbutton.root = root;
  event.xbutton.subwindow = None;
  event.xbutton.time = CurrentTime;
  event.xbutton.x = attr.width - 3;
  event.xbutton.y = attr.height / 2;
  event.xbutton.x_root = event.xbutton.x;
  event.xbutton.y_root = attr.y + event.xbutton.y;
  event.xbutton.state = 0;
  event.xbutton.button = Button1;
  event.xbutton.same_screen = True;

  if (!XSendEvent(dpy, bar, False, ButtonPressMask, &event))
    return 0;
  XFlush(dpy);
  pause_for_wm();
  return 1;
}

int
main(int argc, char **argv)
{
  Display *dpy;
  Window root, bar = None, client;
  Atom status, utf8;
  uint64_t baseline, legacy, normal_hash, warning_hash, critical_hash, multi_hash;
  uint64_t rightmost_hash;
  uint64_t rejected, malformed, invalid_record, duplicate, boundary, oversized;
  const char normal[] = "probe\tnormal\tRAWM-V2-STATUS\n";
  const char warning[] = "probe\twarning\tRAWM-V2-STATUS\n";
  const char critical[] = "probe\tcritical\tRAWM-V2-STATUS\n";
  const char multi[] =
    "cpu\twarning\tcpu:63%\n"
    "clock\tnormal\t12:00\n";
  const char rightmost[] = "clock\tnormal\t12:00\n";
  const char bad_state[] = "probe\tunknown\ttext\n";
  const char duplicate_ids[] =
    "probe\tnormal\tone\n"
    "probe\twarning\ttwo\n";
  unsigned char invalid_utf8[] = {
    'p','r','o','b','e','\t','n','o','r','m','a','l','\t',0xc0,0xaf,'\n'
  };
  unsigned char boundary_text[4096];
  unsigned char oversized_text[4097];
  int disabled = argc == 2 && !strcmp(argv[1], "--disabled");
  const char *click_id =
      argc == 3 && !strcmp(argv[1], "--click-id") ? argv[2] : NULL;
  int tries;

  if (argc > 3 ||
      (argc == 2 && !disabled) ||
      (argc == 3 && !click_id)) {
    fprintf(stderr, "usage: %s [--disabled | --click-id ID]\n", argv[0]);
    return EXIT_FAILURE;
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fputs("status-client: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }
  root = DefaultRootWindow(dpy);
  status = XInternAtom(dpy, "_RAWM_STATUS_V3", False);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);

  client = XCreateSimpleWindow(dpy, root, 40, 40, 320, 200, 0, 0, 0);
  XStoreName(dpy, client, "RAWM-STATUS-CLIENT-TEST");
  XMapWindow(dpy, client);
  XFlush(dpy);
  pause_for_wm();

  for (tries = 0; tries < 50 && bar == None; ++tries) {
    bar = find_bar(dpy, root);
    if (bar == None)
      pause_for_wm();
  }
  if (bar == None) {
    fputs("status-client: cannot find rawm bar window\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  if (click_id) {
    char record[256];
    int length;

    length = snprintf(record, sizeof(record), "%s\tnormal\tCLICK-TARGET\n",
                      click_id);
    if (length <= 0 || (size_t)length >= sizeof(record)) {
      fputs("status-client: click id is too long\n", stderr);
      XCloseDisplay(dpy);
      return EXIT_FAILURE;
    }
    set_status(dpy, root, status, utf8,
               (const unsigned char *)record, (size_t)length);
    if (!click_status(dpy, bar, root)) {
      fputs("status-client: could not click status area\n", stderr);
      XCloseDisplay(dpy);
      return EXIT_FAILURE;
    }
    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
  }

  baseline = checksum_window(dpy, bar);
  XStoreName(dpy, root, "LEGACY-WM-NAME");
  XFlush(dpy);
  pause_for_wm();
  legacy = checksum_window(dpy, bar);
  if (legacy != baseline) {
    fputs("status-client: WM_NAME still affects status rendering\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8,
             (const unsigned char *)normal, strlen(normal));
  normal_hash = checksum_window(dpy, bar);
  if ((!disabled && normal_hash == baseline) ||
      (disabled && normal_hash != baseline)) {
    fputs(disabled
            ? "status-client: disabled status protocol changed the bar\n"
            : "status-client: rawm-v3 status did not change the bar\n",
          stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }
  if (disabled) {
    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
  }

  set_status(dpy, root, status, utf8,
             (const unsigned char *)warning, strlen(warning));
  warning_hash = checksum_window(dpy, bar);
  if (warning_hash == normal_hash) {
    fputs("status-client: warning state did not change status rendering\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8,
             (const unsigned char *)critical, strlen(critical));
  critical_hash = checksum_window(dpy, bar);
  if (critical_hash == normal_hash || critical_hash == warning_hash) {
    fputs("status-client: critical state did not use its own rendering\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8,
             (const unsigned char *)multi, strlen(multi));
  multi_hash = checksum_window(dpy, bar);
  if (multi_hash == baseline || multi_hash == critical_hash) {
    fputs("status-client: multi-segment status was not rendered distinctly\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8,
             (const unsigned char *)rightmost, strlen(rightmost));
  rightmost_hash = checksum_window(dpy, bar);
  if (multi_hash == rightmost_hash) {
    fputs("status-client: selected client title obscured leading status segments\n",
          stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, XA_STRING,
             (const unsigned char *)normal, strlen(normal));
  rejected = checksum_window(dpy, bar);
  if (rejected != baseline) {
    fputs("status-client: non-UTF8_STRING status was accepted\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8, invalid_utf8, sizeof(invalid_utf8));
  malformed = checksum_window(dpy, bar);
  if (malformed != baseline) {
    fputs("status-client: malformed UTF-8 status was accepted\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8,
             (const unsigned char *)bad_state, strlen(bad_state));
  invalid_record = checksum_window(dpy, bar);
  if (invalid_record != baseline) {
    fputs("status-client: unknown semantic state was accepted\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8,
             (const unsigned char *)duplicate_ids, strlen(duplicate_ids));
  duplicate = checksum_window(dpy, bar);
  if (duplicate != baseline) {
    fputs("status-client: duplicate segment ids were accepted\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  boundary_text[0] = 'x';
  boundary_text[1] = '\t';
  memcpy(boundary_text + 2, "normal", 6);
  boundary_text[8] = '\t';
  memset(boundary_text + 9, 'x', 4086);
  boundary_text[4095] = '\n';
  set_status(dpy, root, status, utf8, boundary_text, sizeof(boundary_text));
  boundary = checksum_window(dpy, bar);
  if (boundary == baseline) {
    fputs("status-client: rawm-v3 maximum-size status was rejected\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  oversized_text[0] = 'x';
  oversized_text[1] = '\t';
  memcpy(oversized_text + 2, "normal", 6);
  oversized_text[8] = '\t';
  memset(oversized_text + 9, 'x', 4087);
  oversized_text[4096] = '\n';
  set_status(dpy, root, status, utf8, oversized_text, sizeof(oversized_text));
  oversized = checksum_window(dpy, bar);
  if (oversized != baseline) {
    fputs("status-client: oversized rawm-v3 status was accepted\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
