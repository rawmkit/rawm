/* See LICENSE file for copyright and license details. */

#include "rawmctl.h"

#include <X11/XKBlib.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static int
require_xkb(Display *display)
{
  int opcode, event, error;
  int major = XkbMajorVersion;
  int minor = XkbMinorVersion;

  if (!XkbQueryExtension(display, &opcode, &event, &error, &major, &minor))
  {
    fprintf(stderr, "rawmctl: XKB extension is not available\n");
    return -1;
  }
  return 0;
}

static int
parse_group(const char *text, unsigned int *group)
{
  char *end = NULL;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno || !end || end == text || *end || value > 3 || value > UINT_MAX)
    return -1;

  *group = (unsigned int)value;
  return 0;
}

int
rawmctl_keyboard_get(Display *display)
{
  XkbStateRec state;

  if (require_xkb(display) < 0)
    return EXIT_FAILURE;
  if (XkbGetState(display, XkbUseCoreKbd, &state) != Success)
  {
    fprintf(stderr, "rawmctl: cannot read XKB keyboard state\n");
    return EXIT_FAILURE;
  }

  printf("%u\n", (unsigned int)state.group);
  return EXIT_SUCCESS;
}

int
rawmctl_keyboard_set(Display *display, const char *group_text)
{
  XkbStateRec state;
  unsigned int group;

  if (parse_group(group_text, &group) < 0)
  {
    fprintf(stderr, "rawmctl: keyboard group must be in the range 0..3\n");
    return EXIT_FAILURE;
  }
  if (require_xkb(display) < 0)
    return EXIT_FAILURE;
  if (!XkbLockGroup(display, XkbUseCoreKbd, group))
  {
    fprintf(stderr, "rawmctl: cannot set XKB keyboard group %u\n", group);
    return EXIT_FAILURE;
  }

  XSync(display, False);
  if (XkbGetState(display, XkbUseCoreKbd, &state) != Success)
  {
    fprintf(stderr, "rawmctl: cannot verify XKB keyboard group\n");
    return EXIT_FAILURE;
  }
  if ((unsigned int)state.group != group)
  {
    fprintf(stderr,
            "rawmctl: X server retained keyboard group %u instead of %u\n",
            (unsigned int)state.group, group);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
