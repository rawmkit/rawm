#ifndef RAWMCTL_H
#define RAWMCTL_H

#include <X11/Xlib.h>

int rawmctl_keyboard_get(Display *display);
int rawmctl_keyboard_set(Display *display, const char *group_text);
int rawmctl_window_activate(Display *display, const char *window_text);
int rawmctl_window_list(Display *display);
int rawmctl_window_select(Display *display);

#endif
