/* See LICENSE file for copyright and license details. */

#include "rawmctl.h"

#include <X11/Xlib.h>

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(FILE *stream)
{
  fprintf(stream,
          "usage: rawmctl window list\n"
          "       rawmctl window activate WINDOW\n"
          "       rawmctl window select\n"
          "       rawmctl -v\n");
}

static Display *
open_display(void)
{
  Display *display = XOpenDisplay(NULL);

  if (!display)
    fprintf(stderr, "rawmctl: cannot open X display %s\n", XDisplayName(NULL));
  return display;
}

int
main(int argc, char **argv)
{
  Display *display;
  int result = EXIT_FAILURE;

  (void)setlocale(LC_CTYPE, "");

  if (argc == 2 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")))
  {
    puts("rawmctl " VERSION);
    return EXIT_SUCCESS;
  }
  if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
  {
    usage(stdout);
    return EXIT_SUCCESS;
  }
  if (argc < 3)
  {
    usage(stderr);
    return EXIT_FAILURE;
  }

  display = open_display();
  if (!display)
    return EXIT_FAILURE;

  if (!strcmp(argv[1], "window"))
  {
    if (argc == 3 && !strcmp(argv[2], "list"))
      result = rawmctl_window_list(display);
    else if (argc == 4 && !strcmp(argv[2], "activate"))
      result = rawmctl_window_activate(display, argv[3]);
    else if (argc == 3 && !strcmp(argv[2], "select"))
      result = rawmctl_window_select(display);
    else
      usage(stderr);
  }
  else
    usage(stderr);

  XCloseDisplay(display);
  return result;
}
