#include <X11/Xlib.h>

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef Bool (*FakeMotionEventFn)(Display *, int, int, int, unsigned long);
typedef Bool (*FakeButtonEventFn)(Display *, unsigned int, Bool, unsigned long);

static long
number(const char *s, const char *what)
{
  char *end = NULL;
  long value;

  errno = 0;
  value = strtol(s, &end, 10);
  if (errno || !end || *end) {
    fprintf(stderr, "send-pointer: invalid %s: %s\n", what, s);
    exit(EXIT_FAILURE);
  }
  return value;
}

static void
sleep_ms(long ms)
{
  struct timespec ts;

  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
    ;
}

int
main(int argc, char **argv)
{
  Display *dpy;
  Window root, child;
  int root_x, root_y, win_x, win_y;
  unsigned int mask;
  long button, start_x, start_y, dx, dy;
  void *handle;
  FakeMotionEventFn fake_motion_event;
  FakeButtonEventFn fake_button_event;
  int i;

  if (argc != 6) {
    fprintf(stderr, "usage: %s BUTTON START_X START_Y DX DY\n", argv[0]);
    return EXIT_FAILURE;
  }

  button = number(argv[1], "button");
  start_x = number(argv[2], "start x");
  start_y = number(argv[3], "start y");
  dx = number(argv[4], "delta x");
  dy = number(argv[5], "delta y");
  if (button < 1 || button > 5) {
    fputs("send-pointer: button must be 1..5\n", stderr);
    return EXIT_FAILURE;
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fputs("send-pointer: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }

  handle = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "send-pointer: libXtst.so.6 unavailable: %s\n", dlerror());
    XCloseDisplay(dpy);
    return 77;
  }

  *(void **)(&fake_motion_event) = dlsym(handle, "XTestFakeMotionEvent");
  *(void **)(&fake_button_event) = dlsym(handle, "XTestFakeButtonEvent");
  if (!fake_motion_event || !fake_button_event) {
    fputs("send-pointer: XTest pointer functions unavailable\n", stderr);
    dlclose(handle);
    XCloseDisplay(dpy);
    return 77;
  }

  fake_motion_event(dpy, DefaultScreen(dpy), (int)start_x, (int)start_y,
                    CurrentTime);
  XSync(dpy, False);
  fake_button_event(dpy, (unsigned int)button, True, CurrentTime);
  XSync(dpy, False);

  /*
   * rawm may warp the pointer after a resize press. Give it time to enter
   * the modal interaction loop, then use the actual server pointer as the
   * origin for the requested relative drag.
   */
  sleep_ms(40);
  root = DefaultRootWindow(dpy);
  if (!XQueryPointer(dpy, root, &root, &child, &root_x, &root_y,
                     &win_x, &win_y, &mask)) {
    fputs("send-pointer: cannot query pointer\n", stderr);
    fake_button_event(dpy, (unsigned int)button, False, CurrentTime);
    XSync(dpy, False);
    dlclose(handle);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  for (i = 1; i <= 6; ++i) {
    int x = root_x + (int)(dx * i / 6);
    int y = root_y + (int)(dy * i / 6);

    fake_motion_event(dpy, DefaultScreen(dpy), x, y, CurrentTime);
    XSync(dpy, False);
    sleep_ms(12);
  }

  fake_button_event(dpy, (unsigned int)button, False, CurrentTime);
  XSync(dpy, False);

  dlclose(handle);
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
