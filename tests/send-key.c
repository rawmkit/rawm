#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef Bool (*FakeKeyEventFn)(Display *, unsigned int, Bool, unsigned long);

int
main(int argc, char **argv)
{
  Display *dpy;
  KeySym sym;
  KeyCode code;
  void *handle;
  FakeKeyEventFn fake_key_event;

  if (argc != 2) {
    fprintf(stderr, "usage: %s KEYSYM\n", argv[0]);
    return EXIT_FAILURE;
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fputs("send-key: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }
  sym = XStringToKeysym(argv[1]);
  code = sym == NoSymbol ? 0 : XKeysymToKeycode(dpy, sym);
  if (!code) {
    fprintf(stderr, "send-key: unknown keysym: %s\n", argv[1]);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  handle = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "send-key: libXtst.so.6 unavailable: %s\n", dlerror());
    XCloseDisplay(dpy);
    return 77;
  }
  *(void **)(&fake_key_event) = dlsym(handle, "XTestFakeKeyEvent");
  if (!fake_key_event) {
    fputs("send-key: XTestFakeKeyEvent unavailable\n", stderr);
    XCloseDisplay(dpy);
    dlclose(handle);
    return 77;
  }

  fake_key_event(dpy, code, True, CurrentTime);
  fake_key_event(dpy, code, False, CurrentTime);
  XSync(dpy, False);
  XCloseDisplay(dpy);
  dlclose(handle);
  return EXIT_SUCCESS;
}
