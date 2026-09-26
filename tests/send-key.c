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
  KeyCode *codes;
  void *handle;
  FakeKeyEventFn fake_key_event;
  int i;

  if (argc < 2) {
    fprintf(stderr, "usage: %s KEYSYM [KEYSYM ...]\n", argv[0]);
    return EXIT_FAILURE;
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fputs("send-key: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }

  codes = calloc((size_t)(argc - 1), sizeof(*codes));
  if (!codes) {
    fputs("send-key: out of memory\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  for (i = 1; i < argc; ++i) {
    KeySym sym = XStringToKeysym(argv[i]);
    KeyCode code = sym == NoSymbol ? 0 : XKeysymToKeycode(dpy, sym);

    if (!code) {
      fprintf(stderr, "send-key: unknown keysym: %s\n", argv[i]);
      free(codes);
      XCloseDisplay(dpy);
      return EXIT_FAILURE;
    }
    codes[i - 1] = code;
  }

  handle = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "send-key: libXtst.so.6 unavailable: %s\n", dlerror());
    free(codes);
    XCloseDisplay(dpy);
    return 77;
  }
  *(void **)(&fake_key_event) = dlsym(handle, "XTestFakeKeyEvent");
  if (!fake_key_event) {
    fputs("send-key: XTestFakeKeyEvent unavailable\n", stderr);
    free(codes);
    XCloseDisplay(dpy);
    dlclose(handle);
    return 77;
  }

  for (i = 0; i < argc - 1; ++i)
    fake_key_event(dpy, codes[i], True, CurrentTime);
  for (i = argc - 2; i >= 0; --i)
    fake_key_event(dpy, codes[i], False, CurrentTime);

  XSync(dpy, False);
  free(codes);
  XCloseDisplay(dpy);
  dlclose(handle);
  return EXIT_SUCCESS;
}
