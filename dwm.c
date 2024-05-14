/* See LICENSE file for copyright and license details. */

/* dynamic window manager is designed like any other X client as well.
 * It is driven through handling X events.  In contrast to other X
 * clients, a window manager selects for SubstructureRedirectMask on
 * the root window, to receive events about window (dis-)appearance.
 * Only one X connection at a time is allowed to select for this event
 * mask.
 *
 * The event handlers of dwm are organized in an array which is
 * accessed whenever a new event has been fetched.  This allows event
 * dispatching in O(1) time.
 *
 * Each child of the root window is called a client, except windows
 * which have set the override_redirect flag.  Clients are organized
 * in a linked client list on each monitor, the focus history is
 * remembered through a stack list on each monitor.  Each client
 * contains a bit array to indicate the tags of a client.
 *
 * Keys and tagging rules are organized as arrays and defined in
 * config.h.
 *
 * To understand everything else, start reading main().
 */

#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#ifdef PWKL
# include <X11/XKBlib.h>
#endif
#ifdef XINERAMA
# include <X11/extensions/Xinerama.h>
#endif

/*********************************************************************
 * Macros.
 */

#define BUTTONMASK  (ButtonPressMask | ButtonReleaseMask)

#define CLEANMASK(mask)                                              \
  (    mask                                                          \
   & ~(numlockmask | LockMask)                                       \
   &  (ShiftMask   | ControlMask |                                   \
       Mod1Mask    | Mod2Mask    | Mod3Mask | Mod4Mask | Mod5Mask))

#define INTERSECT(x, y, w, h, m) \
  (  MAX(0, MIN((x) + (w), (m)->wx + (m)->ww)  -  MAX((x), (m)->wx)) \
   * MAX(0, MIN((y) + (h), (m)->wy + (m)->wh)  -  MAX((y), (m)->wy)))

#define ISVISIBLE(C)  ((C->tags & C->mon->tagset[C->mon->seltags]))

#define LENGTH(X)  (sizeof X / sizeof X[0])

#define MAX(A, B)  ((A) > (B) ? (A) : (B))
#define MIN(A, B)  ((A) < (B) ? (A) : (B))

#define MOUSEMASK  (BUTTONMASK|PointerMotionMask)

#define WIDTH(X)   ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)  ((X)->h + 2 * (X)->bw)

#define TAGMASK    ((1 << TAGS) - 1)

#define TEXTW(X)   (textnw(X, strlen(X)) + dc.font.height)

/*********************************************************************
 * Enums & Typedefs.
 */

enum { /* cursor */
  CurNormal, CurResize, CurMove, CurLast
};
enum { /* color */
  ColBorder, ColFG, ColBG, ColLast
};
enum { /* EWMH atoms */
  NetSupported, NetWMName, NetWMState,
  NetWMFullscreen, NetActiveWindow, NetWMWindowType,
  NetWMWindowTypeDialog, NetLast
};
enum { /* default atoms */
  WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast
};
enum { /* clicks */
  ClkTagBar, ClkLtSymbol, ClkStatusText,
#ifdef WINTITLE
  ClkWinTitle,
#endif
  ClkClientWin, ClkRootWin, ClkLast
};

typedef union {
  int            i;
  int           ui;
  float          f;
  const void    *v;
} Arg;

typedef struct {
  unsigned int  click;
  unsigned int  mask;
  unsigned int  button;
  void          (*func)(const Arg *arg);
  const Arg     arg;
} Button;

typedef struct Monitor  Monitor;
typedef struct Client   Client;

struct Client {
  char          name[256];
  float         mina, maxa;
  int           x, y, w, h;
  int           oldx, oldy, oldw, oldh;
  int           basew, baseh, incw, inch, maxw, maxh, minw, minh;
  int           bw, oldbw;
  unsigned int  tags;
  bool          isfixed, isfloating, iscentered, isurgent, neverfocus,
                oldstate, isfullscreen;
  Client        *next;
  Client        *snext;
  Monitor       *mon;
  Window        win;
#ifdef PWKL
  unsigned char kbdgrp;
#endif
};

typedef struct {
  int           x, y, w, h;
  XftColor      norm[ColLast];
  XftColor      sel[ColLast];
  Drawable      drawable;
  GC            gc;
  struct {
    int         ascent;
    int         descent;
    int         height;
    XftFont     *xfont;
  } font;
} DC; /* draw context */

typedef struct {
  unsigned int  mod;
  KeySym        keysym;
  void          (*func)(const Arg *);
  const Arg     arg;
} Key;

typedef struct {
  const char    *symbol;
  void          (*arrange)(Monitor *);
} Layout;

/* Max tag length */
#define MAX_TAGLEN      25

/* Max tag name length: 3 is a tag number with semicolon */
#define MAX_TAGNAMELEN (MAX_TAGLEN - 3)

typedef struct {
  char          tagname[MAX_TAGLEN];
  int           layout_idx;
} CustomTagLayout;

typedef struct Pertag Pertag;

struct Monitor {
  char          ltsymbol[16];
  float         mfact;
  int           nmaster;
  int           num;
  int           by;               /* bar geometry */
  int           mx, my, mw, mh;   /* screen size */
  int           wx, wy, ww, wh;   /* window area */
  unsigned int  seltags;
  unsigned int  sellt;
  unsigned int  tagset[2];
  bool          showbar;
  bool          topbar;
  Client        *clients;
  Client        *sel;
  Client        *stack;
  Monitor       *next;
  Window        barwin;
  const Layout  *lt[2];
  Pertag        *pertag;
};

typedef struct {
  const char    *class;
  const char    *instance;
  const char    *title;
  const char    *role;
  unsigned int  tags;
  bool          iscentered;
  bool          isfloating;
  int           monitor;
} Rule;

/*********************************************************************
 * Function declarations.
 */

static void           applyrules(Client *c);
static bool           applysizehints(Client *c, int *x, int *y,
                                     int *w, int *h, bool interact);
static void           arrange(Monitor *m);
static void           arrangemon(Monitor *m);
static void           attach(Client *c);
static void           attachstack(Client *c);
static void           bstack(Monitor *m);
static void           bstackhoriz(Monitor *m);
static void           buttonpress(XEvent *e);
static void           checkotherwm(void);
static void           cleanup(void);
static void           cleanupmon(Monitor *mon);
static void           clearurgent(Client *c);
static void           clientmessage(XEvent *e);
static void           configure(Client *c);
static void           configurenotify(XEvent *e);
static void           configurerequest(XEvent *e);
static Monitor       *createmon(void);
static void           destroynotify(XEvent *e);
static void           detach(Client *c);
static void           detachstack(Client *c);
static void           die(const char *errstr, ...);
static Monitor       *dirtomon(int dir);
static void           drawbar(Monitor *m);
static void           drawbars(void);
static void           drawsquare(bool filled, bool empty, bool invert,
                                 XftColor col[ColLast]);
static void           drawtext(const char *text, XftColor col[ColLast],
                               bool invert);
static void           enternotify(XEvent *e);
static void           expose(XEvent *e);
static void           focus(Client *c);
static void           focusin(XEvent *e);
static void           focusmon(const Arg *arg);
static void           focusnstack(const Arg *arg);
static void           focusstack(const Arg *arg);
static void           gaplessgrid(Monitor *m);
static XftColor       getcolor(const char *colstr);
static bool           getrootptr(int *x, int *y);
static long           getstate(Window w);
static bool           gettextprop(Window w, Atom atom, char *text,
                                  unsigned int size);
static void           grabbuttons(Client *c, bool focused);
static void           grabkeys(void);
static void           incnmaster(const Arg *arg);
static void           initfont(const char *fontstr);
static void           keypress(XEvent *e);
static void           killclient(const Arg *arg);
static void           manage(Window w, XWindowAttributes *wa);
static void           mappingnotify(XEvent *e);
static void           maprequest(XEvent *e);
static void           monocle(Monitor *m);
static void           motionnotify(XEvent *e);
static void           movemouse(const Arg *arg);
static void           nametag(const Arg *arg);
static Client        *nexttiled(Client *c);
static void           pop(Client *);
static void           propertynotify(XEvent *e);
static void           quit(const Arg *arg);
static Monitor       *recttomon(int x, int y, int w, int h);
static void           resize(Client *c, int x, int y, int w, int h,
                             bool interact);
static void           resizeclient(Client *c, int x, int y, int w, int h);
static void           resizemouse(const Arg *arg);
static void           restack(Monitor *m);
static void           run(void);
static void           scan(void);
static bool           sendevent(Client *c, Atom proto);
static void           sendmon(Client *c, Monitor *m);
#ifdef SESSION_FILE
static void           session_store(void);
static void           session_restore(void);
#endif
static void           setclientstate(Client *c, long state);
static void           setfocus(Client *c);
static void           setfullscreen(Client *c, bool fullscreen);
static void           setlayout(const Arg *arg);
static void           setmfact(const Arg *arg);
static void           setup(void);
static void           showhide(Client *c);
static void           sigchld(int sig);
static void           sighup(int sig);
static void           sigterm(int sig);
static void           spawn(const Arg *arg);
static void           tag(const Arg *arg);
static void           tagmon(const Arg *arg);
static int            textnw(const char *text, unsigned int len);
static void           tile(Monitor *);
static void           togglebar(const Arg *arg);
static void           togglefloating(const Arg *arg);
static void           togglefullscr(const Arg *arg);
static void           toggletag(const Arg *arg);
static void           toggleview(const Arg *arg);
static void           unfocus(Client *c, bool setfocus);
static void           unmanage(Client *c, bool destroyed);
static void           unmapnotify(XEvent *e);
static bool           updategeom(void);
static void           updatebarpos(Monitor *m);
static void           updatebars(void);
static void           updatenumlockmask(void);
static void           updatesizehints(Client *c);
static void           updatestatus(void);
static void           updatewindowtype(Client *c);
static void           updatetitle(Client *c);
static void           updatewmhints(Client *c);
static void           view(const Arg *arg);
static Client        *wintoclient(Window w);
static Monitor       *wintomon(Window w);
static void           winview(const Arg* arg);
static int            xerror(Display *dpy, XErrorEvent *ee);
static int            xerrordummy(Display *dpy, XErrorEvent *ee);
static int            xerrorstart(Display *dpy, XErrorEvent *ee);
static void           zoom(const Arg *arg);

/*********************************************************************
 * Global variables.
 */

static const char     broken[] = "broken";
static char           stext[256];
static int            screen;
static int            sw, sh; /* X display screen geometry width, height */
static int            bh, blw = 0; /* bar geometry */
static int           (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int   numlockmask = 0;
static void          (*handler[LASTEvent]) (XEvent *) = {
  [ButtonPress]       = buttonpress,
  [ClientMessage]     = clientmessage,
  [ConfigureRequest]  = configurerequest,
  [ConfigureNotify]   = configurenotify,
  [DestroyNotify]     = destroynotify,
  [EnterNotify]       = enternotify,
  [Expose]            = expose,
  [FocusIn]           = focusin,
  [KeyPress]          = keypress,
  [MappingNotify]     = mappingnotify,
  [MapRequest]        = maprequest,
  [MotionNotify]      = motionnotify,
  [PropertyNotify]    = propertynotify,
  [UnmapNotify]       = unmapnotify
};
static Atom           wmatom[WMLast], netatom[NetLast];
static bool           restart = false;
static bool           running = true;
static Cursor         cursor[CurLast];
static Display       *dpy;
static DC             dc;
static Monitor       *mons = NULL, *selmon = NULL;
static Window         root;

/* Configuration, allows nested code to access above variables. */
#include "config.h"

static unsigned int   opacity = defaultopacity * 0xffffffff;

struct Pertag {
  unsigned int  curtag, prevtag;     /* current and previous tag           */
  int           nmasters[TAGS + 1];  /* number of windows in master area   */
  float         mfacts[TAGS + 1];    /* mfacts per tag                     */
  unsigned int  sellts[TAGS + 1];    /* selected layouts                   */
  const Layout *ltidxs[TAGS + 1][2]; /* matrix of tags and layouts indexes */
  bool          showbars[TAGS + 1];  /* display bar for the current tag    */
};

/* Compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags {
  char limitexceeded[TAGS > 31 ? -1 : 1];
};

/*********************************************************************
 * Function implementations.
 */

char *
getprop(Window w, const char *prop)
{
  Atom            atom, adummy;
  unsigned char  *val = NULL;
  int             idummy;
  unsigned long   ldummy;

  atom = XInternAtom(dpy, prop, true);

  if (atom)
    XGetWindowProperty(dpy, w, atom, 0, BUFSIZ, false, XA_STRING,
                       &adummy, &idummy, &ldummy, &ldummy, &val);

  return (char *)val;
}

static void
applyrules(Client *c)
{
  unsigned int   i;
  const char    *class, *instance, *role;
  const Rule    *r;
  Monitor       *m;
  XClassHint     ch = { NULL, NULL };

  /* rule matching */
  c->isfloating = 0;
  c->tags = 0;
  XGetClassHint(dpy, c->win, &ch);

  class     = ch.res_class ? ch.res_class : broken;
  instance  = ch.res_name  ? ch.res_name  : broken;
  role      = (role = getprop(c->win, "WM_WINDOW_ROLE"))
            ?  role
            :  broken;

  for (i = 0; i < LENGTH(rules); i++)
  {
    r = &rules[i];

    if (   (!r->title    || strstr(c->name,  r->title))
        && (!r->class    || strstr(class,    r->class))
        && (!r->instance || strstr(instance, r->instance))
        && (!r->role     || strstr(role,     r->role)))
    {
      c->iscentered  = r->iscentered;
      c->isfloating  = r->isfloating;
      c->tags       |= r->tags;

      for (m = mons; m && m->num != r->monitor; m = m->next)
        /* NOTHING */;

      if (m)
        c->mon = m;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);

  if (ch.res_name)
    XFree(ch.res_name);

  c->tags = (c->tags & TAGMASK)
          ? (c->tags & TAGMASK)
          :  c->mon->tagset[c->mon->seltags];
}

static bool
applysizehints(Client *c, int *x, int *y, int *w, int *h,
               bool interact)
{
  Monitor *m = c->mon;

  /* set minimum possible */
  *w = MAX(1, *w);
  *h = MAX(1, *h);

  if (interact)
  {
    if (*x > sw)                  *x = sw - WIDTH(c);
    if (*y > sh)                  *y = sh - HEIGHT(c);
    if (*x + *w + 2 * c->bw < 0)  *x = 0;
    if (*y + *h + 2 * c->bw < 0)  *y = 0;
  }
  else
  {
    if (*x >= m->wx + m->ww)           *x = m->wx + m->ww - WIDTH(c);
    if (*y >= m->wy + m->wh)           *y = m->wy + m->wh - HEIGHT(c);
    if (*x + *w + 2 * c->bw <= m->wx)  *x = m->wx;
    if (*y + *h + 2 * c->bw <= m->wy)  *y = m->wy;
  }

  if (*h < bh)
    *h = bh;

  if (*w < bh)
    *w = bh;

  if (    resizehints
      ||  c->isfloating
      || !c->mon->lt[c->mon->sellt]->arrange)
  {
    /* see last two sentences in ICCCM 4.1.2.3 */
    bool baseismin = c->basew == c->minw && c->baseh == c->minh;
    if (!baseismin)
    {
      /* temporarily remove base dimensions */
      *w -= c->basew;
      *h -= c->baseh;
    }

    /* adjust for aspect limits */
    if (c->mina > 0 && c->maxa > 0)
    {
      if      (c->maxa < (float)*w / *h)  *w = *h * c->maxa + 0.5;
      else if (c->mina < (float)*h / *w)  *h = *w * c->mina + 0.5;
    }

    if (baseismin)
    {
      /* increment calculation requires this */
      *w -= c->basew;
      *h -= c->baseh;
    }

    /* adjust for increment value */
    if (c->incw)  *w -= *w % c->incw;
    if (c->inch)  *h -= *h % c->inch;

    /* restore base dimensions */
    *w = MAX(*w + c->basew, c->minw);
    *h = MAX(*h + c->baseh, c->minh);

    if (c->maxw)  *w = MIN(*w, c->maxw);
    if (c->maxh)  *h = MIN(*h, c->maxh);
  }

  return    *x != c->x
         || *y != c->y
         || *w != c->w
         || *h != c->h;
}

static void
arrange(Monitor *m)
{
  if (m)
    showhide(m->stack);
  else
    for (m = mons; m; m = m->next)
      showhide(m->stack);

  if (m)
    arrangemon(m);
  else
    for (m = mons; m; m = m->next)
      arrangemon(m);
}

static void
arrangemon(Monitor *m)
{
  int n = 0;
  Client *c;
  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), n++)
    /* NOTHING */;

  if (  (   m->lt[m->sellt]->arrange != monocle
         && n > 1)
      || !m->lt[m->sellt]->arrange)
  {
    for (c = m->clients; c; c = c->next)
    {
      if (   ISVISIBLE(c)
          && (   !m->lt[m->sellt]->arrange
              || !c->isfloating)
          && (c->bw != borderpx))
      {
        c->oldbw = c->bw;
        c->bw    = borderpx;
        resizeclient(c,
                     m->wx,
                     m->wy,
                     m->ww - (2 * c->bw),
                     m->wh - (2 * c->bw));
      }
    }

    if (m->lt[m->sellt]->arrange)
        m->lt[m->sellt]->arrange(m);
  }
  else
    monocle(m);

  restack(m);
}

static void
attach(Client *c)
{
  c->next         = c->mon->clients;
  c->mon->clients = c;
}

static void
attachstack(Client *c)
{
  c->snext      = c->mon->stack;
  c->mon->stack = c;
}

static void
bstack(Monitor *m)
{
  int     w, h, mh, mx, tx, ty, tw;
  int     i, n;
  Client *c;

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), n++)
    /* NOTHING */;

  if (n == 0)
    return;

  if (n > m->nmaster)
  {
    mh = m->nmaster ? m->mfact * m->wh : 0;
    tw = m->ww / (n - m->nmaster);
    ty = m->wy + mh;
  }
  else
  {
    mh = m->wh;
    tw = m->ww;
    ty = m->wy;
  }

  for (i = mx = 0, tx = m->wx, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), i++)
  {
    if (i < m->nmaster)
    {
      w = (m->ww - mx) / (MIN(n, m->nmaster) - i);
      resize(c,
             m->wx + mx,
             m->wy,
             w  - (2 * c->bw),
             mh - (2 * c->bw),
             false);
      mx += WIDTH(c);
    }
    else
    {
      h = m->wh - mh;
      resize(c,
             tx,
             ty,
             tw - (2 * c->bw),
             h  - (2 * c->bw),
             false);
      if (tw != m->ww)
        tx += WIDTH(c);
    }
  }
}

static void
bstackhoriz(Monitor *m)
{
  int     w, mh, mx, tx, ty, th;
  int     i, n;
  Client *c;

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), n++)
    /* NOTHING */;

  if (n == 0)
    return;

  if (n > m->nmaster)
  {
    mh =  m->nmaster ? m->mfact * m->wh : 0;
    th = (m->wh - mh) / (n - m->nmaster);
    ty =  m->wy + mh;
  }
  else
  {
    th = mh = m->wh;
    ty =      m->wy;
  }

  for (i = mx = 0, tx = m->wx, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), i++)
  {
    if (i < m->nmaster)
    {
      w = (m->ww - mx) / (MIN(n, m->nmaster) - i);
      resize(c,
             m->wx + mx,
             m->wy,
             w  - (2 * c->bw),
             mh - (2 * c->bw),
             false);
      mx += WIDTH(c);
    }
    else
    {
      resize(c,
             tx,
             ty,
             m->ww - (2 * c->bw),
             th    - (2 * c->bw),
             false);
      if (th != m->wh)
        ty += HEIGHT(c);
    }
  }
}

static void
buttonpress(XEvent *e)
{
  Arg                  arg   = {0};
  Client              *c;
  Monitor             *m;
  XButtonPressedEvent *ev    = &e->xbutton;
  unsigned int         click = ClkRootWin;

  /* focus monitor if necessary */
  if (  (m = wintomon(ev->window))
      && m != selmon)
  {
    unfocus(selmon->sel, true);
    selmon = m;
    focus(NULL);
  }

  if (ev->window == selmon->barwin)
  {
    unsigned int i = 0, occ = 0;
    int          x = 0;
    for (c = m->clients; c; c = c->next)
      occ |= (c->tags == 255) ? 0 : c->tags;

    do
    {
      /* do not reserve space for vacant tags */
      if (!(occ & 1 << i || m->tagset[m->seltags] & 1 << i))
        continue;

      x += TEXTW(tags[m->num][i].tagname);

    } while (ev->x >= x && ++i < TAGS);

    if (i < TAGS)
    {
      click  = ClkTagBar;
      arg.ui = 1 << i;
    }
    else if (ev->x < x + blw)
      click = ClkLtSymbol;
#ifdef WINTITLE
    else if (ev->x > selmon->ww - TEXTW(stext))
      click = ClkStatusText;
    else
      click = ClkWinTitle;
#else
    else
      click = ClkStatusText;
#endif
  }
  else if ((c = wintoclient(ev->window)))
  {
    focus(c);
    click = ClkClientWin;
  }

  for (size_t i = 0; i < LENGTH(buttons); i++)
  {
    if (   click                      == buttons[i].click
        && buttons[i].func
        && buttons[i].button          == ev->button
        && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
    {
      buttons[i].func(
        (click == ClkTagBar && buttons[i].arg.i == 0)
          ? &arg
          : &buttons[i].arg);
    }
  }
}

static void
checkotherwm(void)
{
  xerrorxlib = XSetErrorHandler(xerrorstart);

  /* this causes an error if some other window manager is running */
  XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
  XSync(dpy, false);
  XSetErrorHandler(xerror);
  XSync(dpy, false);
}

static void
cleanup(void)
{
  Arg      a   = { .ui = ~0 };
  Layout   foo = { "", NULL };
  Monitor *m;

  view(&a);
  selmon->lt[selmon->sellt] = &foo;

  for (m = mons; m; m = m->next)
  {
    while (m->stack)
      unmanage(m->stack, false);
  }

  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  XFreePixmap(dpy, dc.drawable);
  XFreeGC(dpy, dc.gc);
  XFreeCursor(dpy, cursor[CurNormal]);
  XFreeCursor(dpy, cursor[CurResize]);
  XFreeCursor(dpy, cursor[CurMove]);

  while (mons)
    cleanupmon(mons);

  XSync(dpy, false);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

static void
cleanupmon(Monitor *mon)
{
  Monitor *m;

  if (mon == mons)
    mons = mons->next;
  else
  {
    for (m = mons;  m && m->next != mon;  m = m->next)
      /* NOTHING */;
    m->next = mon->next;
  }

  XUnmapWindow(dpy, mon->barwin);
  XDestroyWindow(dpy, mon->barwin);
  free(mon);
}

static void
clearurgent(Client *c)
{
  XWMHints *wmh;

  c->isurgent = false;

  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;

  wmh->flags &= ~XUrgencyHint;
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

static void
clientmessage(XEvent *e)
{
  XClientMessageEvent *cme = &e->xclient;
  Client              *c   = wintoclient(cme->window);

  if (!c)
    return;

  if (cme->message_type == netatom[NetWMState])
  {
    if (   cme->data.l[1] == netatom[NetWMFullscreen]
        || cme->data.l[2] == netatom[NetWMFullscreen] )
    {
      setfullscreen(c,
                    (       cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                     || (   cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */
                         && !c->isfullscreen  )));
    }
  }
  else if (cme->message_type == netatom[NetActiveWindow])
  {
    if (!ISVISIBLE(c))
    {
      c->mon->seltags ^= 1;
      c->mon->tagset[c->mon->seltags] = c->tags;
    }
    pop(c);
  }
}

static void
configure(Client *c)
{
  XConfigureEvent ce;

  ce.type         = ConfigureNotify;
  ce.display      = dpy;
  ce.event        = c->win;
  ce.window       = c->win;
  ce.x            = c->x;
  ce.y            = c->y;
  ce.width        = c->w;
  ce.height       = c->h;
  ce.border_width = c->bw;
  ce.above        = None;

  ce.override_redirect = false;
  XSendEvent(dpy, c->win, false, StructureNotifyMask, (XEvent *)&ce);
}

static void
configurenotify(XEvent *e)
{
  Monitor         *m;
  XConfigureEvent *ev = &e->xconfigure;

  if (ev->window == root)
  {
    bool dirty = (sw != ev->width);
    sw = ev->width;
    sh = ev->height;

    if (updategeom() || dirty)
    {
      if(dc.drawable != 0)
        XFreePixmap(dpy, dc.drawable);

      dc.drawable = XCreatePixmap(dpy,
                                  root,
                                  sw,
                                  bh,
                                  DefaultDepth(dpy, screen));
      updatebars();

      for (m = mons;  m;  m = m->next)
        XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);

      focus(NULL);
      arrange(NULL);
    }
  }
}

static void
configurerequest(XEvent *e)
{
  Client                 *c;
  Monitor                *m;
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges          wc;

  if ((c = wintoclient(ev->window)))
  {
    if (ev->value_mask & CWBorderWidth)
      c->bw = ev->border_width;
    else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange)
    {
      m = c->mon;

      if (ev->value_mask & CWX)
      {
        c->oldx  = c->x;
        c->x     = m->mx + ev->x;
      }
      if (ev->value_mask & CWY)
      {
        c->oldy  = c->y;
        c->y     = m->my + ev->y;
      }
      if (ev->value_mask & CWWidth)
      {
        c->oldw  = c->w;
        c->w     = ev->width;
      }
      if (ev->value_mask & CWHeight)
      {
        c->oldh  = c->h;
        c->h     = ev->height;
      }

      if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
        c->x = m->mx + (m->mw / 2 - WIDTH(c)  / 2); /* center in x direction */

      if ((c->y + c->h) > m->my + m->mh && c->isfloating)
        c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */

      if (    (ev->value_mask & (CWX     | CWY))
          && !(ev->value_mask & (CWWidth | CWHeight)))
        configure(c);

      if (ISVISIBLE(c))
        XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    }
    else
      configure(c);
  }
  else
  {
    wc.x             = ev->x;
    wc.y             = ev->y;
    wc.width         = ev->width;
    wc.height        = ev->height;
    wc.border_width  = ev->border_width;
    wc.sibling       = ev->above;
    wc.stack_mode    = ev->detail;

    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
  }

  XSync(dpy, false);
}

static Monitor *
createmon(void)
{
  Monitor *m;
  int      i;

  if (!(m = (Monitor *)calloc(1, sizeof(Monitor))))
    die("fatal: could not malloc() %u bytes\n", sizeof(Monitor));

  m->tagset[0]  = m->tagset[1] = 1;
  m->mfact      = mfact;
  m->nmaster    = nmaster;
  m->showbar    = showbar;
  m->topbar     = topbar;
  m->lt[0]      = &layouts[ tags[m->num][0].layout_idx ]; /* current */
  m->lt[1]      = &layouts[1 % LENGTH(layouts)];          /* previous = float */

  strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);

  if (!(m->pertag = (Pertag *)calloc(1, sizeof(Pertag))))
    die("fatal: could not malloc() %u bytes\n", sizeof(Pertag));

  m->pertag->curtag = m->pertag->prevtag = 1;

  for (i = 0; i <= TAGS; i++)
  {
    m->pertag->nmasters[i] = m->nmaster; /* init nmaster */
    m->pertag->mfacts[i]   = m->mfact;   /* init mfacts  */

    /*
     * init layouts
     */

    if ( i == 0 )
    {
      /* view all windows in gapless grid layout */
      m->pertag->ltidxs[0][0] = &layouts[5]; /* current layout */
      m->pertag->ltidxs[0][1] = &layouts[2]; /* previous layout (monocle) */
    }
    else
    {
      m->pertag->ltidxs[i][0] = &layouts[ tags[m->num][i-1].layout_idx ];
      m->pertag->ltidxs[i][1] = &layouts[1 % LENGTH(layouts)];
    }
    m->pertag->sellts[i]    = m->sellt;
    m->pertag->showbars[i]  = m->showbar; /* init showbar */
  }

  return m;
}

static void
destroynotify(XEvent *e)
{
  Client              *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = wintoclient(ev->window)))
    unmanage(c, true);
}

static void
detach(Client *c)
{
  Client **tc;

  for (tc = &c->mon->clients;
      *tc && (*tc != c);
       tc = &(*tc)->next)
    /* NOTHING */;

  *tc = c->next;
}

static void
detachstack(Client *c)
{
  Client **tc, *t;

  for (tc = &c->mon->stack;
      *tc && (*tc != c);
       tc = &(*tc)->snext)
    /* NOTHING */;

  *tc = c->snext;

  if (c == c->mon->sel)
  {
    for (t = c->mon->stack;
         t && !ISVISIBLE(t);
         t = t->snext)
      /* NOTHING */;

    c->mon->sel = t;
  }
}

static void
die(const char *errstr, ...)
{
  va_list ap;

  va_start(ap, errstr);
  vfprintf(stderr, errstr, ap);
  va_end(ap);
  exit(EXIT_FAILURE);
}

static Monitor *
dirtomon(int dir)
{
  Monitor *m = NULL;

  if (dir > 0)
  {
    if (!(m = selmon->next))
      m = mons;
  }
  else if (selmon == mons)
  {
    for (m = mons;  m->next;            m = m->next)
      /* NOTHING */;
  }
  else
  {
    for (m = mons;  m->next != selmon;  m = m->next)
      /* NOTHING */;
  }

  return m;
}

static void
drawbar(Monitor *m)
{
  int           x;
  unsigned int  i, occ = 0, urg = 0;
  XftColor     *col;
  Client       *c;

  for (c = m->clients; c; c = c->next)
  {
    occ |= c->tags == 255 ? 0 : c->tags;
    if (c->isurgent)
      urg |= c->tags;
  }

  dc.x = 0;

  for (i = 0; i < TAGS; i++)
  {
    /* do not draw vacant tags */
    if (!(occ & 1 << i || m->tagset[m->seltags] & 1 << i))
      continue;

    dc.w = TEXTW(tags[m->num][i].tagname);

    col = (m->tagset[m->seltags] & 1 << i)
        ?  dc.sel
        :  dc.norm;

    drawtext(tags[m->num][i].tagname,
             col,
             urg & 1 << i);

    drawsquare(   m == selmon
               && selmon->sel
               && selmon->sel->tags & 1 << i,
               0,
               urg & 1 << i,
               col);

    dc.x += dc.w;
  }

  /* tile master count */
  if (m->lt[m->sellt]->arrange == tile)
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "[]= %d", m->nmaster);

  /* float/monocle count */
  else if (   m->lt[m->sellt]->arrange == monocle
           || m->lt[m->sellt]->arrange == NULL)
  {
    unsigned int i = 0, j = 0;

    for (c = selmon->clients;  c;  c = c->next)
    {
      if (ISVISIBLE(c))
      {
        i++;
        if (c->win == selmon->sel->win)
          j = i;
      }
    }

    if (m->lt[m->sellt]->arrange == NULL)
      snprintf(m->ltsymbol, sizeof m->ltsymbol, "<%u/%u>", j, i);
    else
      snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%u/%u]", j, i);
  }

  else if (m->lt[m->sellt]->arrange == bstack)
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "TTT %u", m->nmaster);

  else if (m->lt[m->sellt]->arrange == bstackhoriz)
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "=== %u", m->nmaster);

  else if (m->lt[m->sellt]->arrange == gaplessgrid)
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "###");

  dc.w  = blw = TEXTW(m->ltsymbol);
  drawtext(m->ltsymbol, dc.norm, false);
  dc.x += dc.w;
  x     = dc.x;

  if (m == selmon)
  {
    /* status is only drawn on selected monitor */
    dc.w = TEXTW(stext);
    dc.x = m->ww - dc.w;

    if (dc.x < x)
    {
      dc.x = x;
      dc.w = m->ww - x;
    }
    drawtext(stext, dc.norm, false);
  }
  else
    dc.x = m->ww;

  if ((dc.w = dc.x - x) > bh)
  {
    dc.x = x;
#ifdef WINTITLE
    if (m->sel)
    {
      /*
       * XXX
       * col = m == selmon ? dc.sel : dc.norm;
       */
      drawtext(m->sel->name, /*col*/dc.norm, false);
      drawsquare(m->sel->isfixed,
                 m->sel->isfloating,
                 false,
                 /*col*/dc.sel);
    }
    else
#endif
      drawtext(NULL, dc.norm, false);
  }

  XCopyArea(dpy, dc.drawable, m->barwin, dc.gc, 0, 0, m->ww, bh, 0, 0);
  XSync(dpy, false);
}

static void
drawbars(void)
{
  Monitor *m;

  for (m = mons; m; m = m->next)
    drawbar(m);
}

static void
drawsquare(bool filled, bool empty, bool invert, XftColor col[ColLast])
{
  int x;

  XSetForeground(dpy, dc.gc, col[invert ? ColBG : ColFG].pixel);

  x = (dc.font.ascent + dc.font.descent + 2) / 4;

  if (filled)
    XFillRectangle(dpy, dc.drawable, dc.gc, dc.x+1, dc.y+1, x+1, x+1);
  else if (empty)
    XDrawRectangle(dpy, dc.drawable, dc.gc, dc.x+1, dc.y+1, x,   x);
}

static void
drawtext(const char *text, XftColor col[ColLast], bool invert)
{
  char     buf[256];
  int      x, y, h, len, olen;
  XftDraw *d;

  XSetForeground(dpy, dc.gc, col[invert ? ColFG : ColBG].pixel);
  XFillRectangle(dpy, dc.drawable, dc.gc, dc.x, dc.y, dc.w, dc.h);

  if (!text)
    return;

  olen = strlen(text);
  h = dc.font.ascent + dc.font.descent;
  y = dc.y + (dc.h / 2) - (h / 2) + dc.font.ascent;
  x = dc.x + (h / 2);

  /* shorten text if necessary */
  for (len = MIN(olen, sizeof buf);
       len && (textnw(text, len) > (dc.w - h));
       len--)
    /* NOTHING */;

  if (!len)
    return;

  memcpy(buf, text, len);

  if (len < olen)
  {
    for (int i = len;  i && (i > (len - 3));  buf[--i] = '.')
      /* NOTHING */;
  }

  d = XftDrawCreate(dpy,
                    dc.drawable,
                    DefaultVisual(dpy, screen),
                    DefaultColormap(dpy, screen));

  XftDrawStringUtf8(d,
                    &col[invert ? ColBG : ColFG],
                    dc.font.xfont,
                    x,
                    y,
                    (XftChar8 *)buf,
                    len);

  XftDrawDestroy(d);
}

static void
enternotify(XEvent *e)
{
  Client          *c;
  Monitor         *m;
  XCrossingEvent  *ev = &e->xcrossing;

  if (   (   ev->mode   != NotifyNormal
          || ev->detail == NotifyInferior)
      && (   ev->window != root))
    return;

  c = wintoclient(ev->window);

  m = c ? c->mon : wintomon(ev->window);

  if (m != selmon)
  {
    unfocus(selmon->sel, true);
    selmon = m;
  }
  else if (!c || c == selmon->sel)
    return;

  focus(c);
}

static void
expose(XEvent *e)
{
  Monitor      *m;
  XExposeEvent *ev = &e->xexpose;

  if (    ev->count == 0
      && (m = wintomon(ev->window)))
    drawbar(m);
}

static void
focus(Client *c)
{
  if (!c || !ISVISIBLE(c))
  {
    for (c = selmon->stack;  c && !ISVISIBLE(c);  c = c->snext)
      /* NOTHING */;
  }

  /* was if(selmon->sel) */
  if (    selmon->sel
      && (selmon->sel != c))
    unfocus(selmon->sel, false);

  if (c)
  {
    if (c->mon != selmon)
      selmon = c->mon;

    if (c->isurgent)
      clearurgent(c);

#ifdef PWKL
    XkbLockGroup(dpy, XkbUseCoreKbd, c->kbdgrp);
#endif

    detachstack(c);
    attachstack(c);
    grabbuttons(c, true);
    XSetWindowBorder(dpy, c->win, dc.sel[ColBorder].pixel);
    setfocus(c);
  }
  else
  {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }

  selmon->sel = c;
  drawbars();
}

static void
focusin(XEvent *e)
{ /* there are some broken focus acquiring clients */
  XFocusChangeEvent *ev = &e->xfocus;

  if (selmon->sel && (ev->window != selmon->sel->win))
    setfocus(selmon->sel);
}

static void
focusmon(const Arg *arg)
{
  Monitor *m;

  if (!mons->next)
    return;

  if ((m = dirtomon(arg->i)) == selmon)
    return;

  unfocus(selmon->sel, true);
  selmon = m;
  focus(NULL);
}

static void
focusnstack(const Arg *arg)
{
  Client *c = NULL,  *i;
  int     j;

  if (!selmon->sel || (arg->i < 0))
    return;

  for (j = 1, i = selmon->clients;
       i;
       i = i->next, j++)
  {
    if (ISVISIBLE(i) && (j == arg->i))
      c = i;
  }

  if (!c)
  {
    for (j = 1;  i;  i = i->next, j++)
    {
      if (ISVISIBLE(i) && (j == arg->i))
        c = i;
    }
  }

  if (c)
  {
    focus(c);
    restack(selmon);
  }
}

static void
focusstack(const Arg *arg)
{
  Client *c = NULL, *i;

  if (!selmon->sel)
    return;

  if (arg->i > 0)
  {
    for (c = selmon->sel->next;  c && !ISVISIBLE(c);  c = c->next)
      /* NOTHING */;

    if (!c)
    {
      for (c = selmon->clients;  c && !ISVISIBLE(c);  c = c->next)
        /* NOTHING */;
    }
  }
  else
  {
    for (i = selmon->clients;  i != selmon->sel;  i = i->next)
    {
      if (ISVISIBLE(i))
        c = i;
    }

    if (!c)
    {
      for (;  i;  i = i->next)
      {
        if (ISVISIBLE(i))
          c = i;
      }
    }
  }

  if (c)
  {
    focus(c);
    restack(selmon);
  }
}

static void
gaplessgrid(Monitor *m)
{
  int     n, cols, rows, cn, rn, i, cx, cy, cw, ch;
  Client *c;

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next))
    n++;

  if (n == 0)
    return;

  /* grid dimensions */
  for (cols = 0; cols <= n/2; cols++)
  {
    if ((cols * cols) >= n)
      break;
  }
  if (n == 5)
  {
    /* set layout against the general calculation:
     * not 1:2:2, but 2:3 */
    cols = 2;
  }

  rows = n/cols;

  /* window geometries */
  cw = cols ? (m->ww / cols) : m->ww;
  cn = 0; /* current column number */
  rn = 0; /* current row number    */
  for (i = 0, c = nexttiled(m->clients);
       c;
       i++, c = nexttiled(c->next))
  {
    if (((i / rows) + 1) > (cols - (n % cols)))
      rows = (n / cols) + 1;

    ch = rows ? (m->wh / rows) : m->wh;
    cx = m->wx + cn*cw;
    cy = m->wy + rn*ch;
    resize(c,
           cx,
           cy,
           cw - 2 * c->bw,
           ch - 2 * c->bw,
           false);
    rn++;

    if (rn >= rows)
    {
      rn = 0;
      cn++;
    }
  }
}

static Atom
getatomprop(Client *c, Atom prop)
{
  int            di;
  unsigned long  dl;
  unsigned char *p = NULL;
  Atom           da, atom = None;

  if (   XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom,
                            False, XA_ATOM, &da, &di, &dl, &dl, &p
                            ) == Success
      && p)
  {
    atom = *(Atom *)p;
    XFree(p);
  }

  return atom;
}

static XftColor
getcolor(const char *colstr)
{
  Colormap cmap = DefaultColormap(dpy, screen);
  XftColor color;

  if (!XftColorAllocName(dpy,
                         DefaultVisual(dpy, screen),
                         cmap, /* DefaultColormap(dpy, screen), */
                         colstr,
                         &color))
    die("error, cannot allocate color '%s'\n", colstr);

  return color;
}

static bool
getrootptr(int *x, int *y)
{
  int          di;
  unsigned int dui;
  Window       dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

static long
getstate(Window w)
{
  int            format;
  long           result = -1;
  unsigned char *p = NULL;
  unsigned long  n, extra;
  Atom           real;

  if (XGetWindowProperty(dpy,
                         w,
                         wmatom[WMState],
                         0L,
                         2L,
                         false,
                         wmatom[WMState],
                         &real,
                         &format,
                         &n,
                         &extra,
                         (unsigned char **)&p)  !=  Success)
    return -1;

  if (n != 0)
    result = *p;

  XFree(p);

  return result;
}

static bool
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
  int             n;
  char          **list = NULL;
  XTextProperty   name;

  if (!text || size == 0)
    return false;

  text[0] = '\0';
  XGetTextProperty(dpy, w, &name, atom);

  if (!name.nitems)
    return false;

  if (name.encoding == XA_STRING)
    strncpy(text, (char *)name.value, size - 1);
  else
  {
    if (   (   XmbTextPropertyToTextList(dpy, &name, &list, &n)
            >= Success )
        && n > 0
        && *list
       )
    {
      strncpy(text, *list, size - 1);
      XFreeStringList(list);
    }
  }

  text[size - 1] = '\0';
  XFree(name.value);

  return true;
}

/* TODO: see upstream workaround */
static void
grabbuttons(Client *c, bool focused)
{
  updatenumlockmask();

  {
    unsigned int modifiers[] = {
      0,
      LockMask,
      numlockmask,
      numlockmask | LockMask
    };

    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);

    if (focused)
    {
      for (unsigned int i = 0; i < LENGTH(buttons); i++)
      {
        if (buttons[i].click == ClkClientWin)
        {
          for (unsigned int j = 0; j < LENGTH(modifiers); j++)
            XGrabButton(dpy,
                        buttons[i].button,
                        buttons[i].mask | modifiers[j],
                        c->win,
                        false,
                        BUTTONMASK,
                        GrabModeAsync,
                        GrabModeSync,
                        None,
                        None);
        }
      }
    }
    else
      XGrabButton(dpy,
                  AnyButton,
                  AnyModifier,
                  c->win,
                  false,
                  BUTTONMASK,
                  GrabModeAsync,
                  GrabModeSync,
                  None,
                  None);
  }
}

static void
grabkeys(void)
{
  updatenumlockmask();

  {
    unsigned int modifiers[] = {
      0,
      LockMask,
      numlockmask,
      numlockmask | LockMask
    };
    KeyCode code;

    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    for (unsigned int i = 0; i < LENGTH(keys); i++)
    {
      if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
      {
        for (unsigned int j = 0; j < LENGTH(modifiers); j++)
          XGrabKey(dpy,
                   code,
                   keys[i].mod | modifiers[j],
                   root,
                   true,
                   GrabModeAsync,
                   GrabModeAsync);
      }
    }
  }
}

static void
incnmaster(const Arg *arg)
{
  selmon->nmaster =
    selmon->pertag->nmasters[selmon->pertag->curtag] =
      MAX(selmon->nmaster + arg->i, 0);

  arrange(selmon);
}

static void
initfont(const char *fontstr)
{
  if (   !(dc.font.xfont = XftFontOpenName(dpy, screen, fontstr))
      && !(dc.font.xfont = XftFontOpenName(dpy, screen, "fixed")))
    die("error, cannot load font: '%s'\n", fontstr);

  dc.font.ascent  = dc.font.xfont->ascent;
  dc.font.descent = dc.font.xfont->descent;
  dc.font.height  = dc.font.ascent + dc.font.descent;
}

#ifdef XINERAMA
static bool
isuniquegeom(XineramaScreenInfo *unique, size_t n,
             XineramaScreenInfo *info)
{
  while (n--)
  {
    if (   unique[n].x_org  == info->x_org
        && unique[n].y_org  == info->y_org
        && unique[n].width  == info->width
        && unique[n].height == info->height
       )
      return false;
  }
  return true;
}
#endif /* XINERAMA */

static void
keypress(XEvent *e)
{
  unsigned int  i;
  KeySym        keysym;
  XKeyEvent    *ev;

  ev     = &e->xkey;
  keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);

  for (i = 0; i < LENGTH(keys); i++)
  {
    if (   (keysym == keys[i].keysym)
        && (CLEANMASK(keys[i].mod) == CLEANMASK(ev->state))
        &&  keys[i].func)
      keys[i].func( &(keys[i].arg) );
  }
}

static void
killclient(__attribute__((unused)) const Arg *arg)
{
  if (!selmon->sel)
    return;

  if (!sendevent(selmon->sel, wmatom[WMDelete]))
  {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selmon->sel->win);
    XSync(dpy, false);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

static void
manage(Window w, XWindowAttributes *wa)
{
  Client         *c, *t = NULL;
  Window          trans = None;
  XWindowChanges  wc;
#ifdef PWKL
  XkbStateRec     kbd_state;
#endif

  if (!(c = calloc(1, sizeof(Client))))
    die("fatal: could not malloc() %u bytes\n", sizeof(Client));

  c->win = w;
  updatetitle(c);

  if (   defaultopacity >= 0
      && defaultopacity <= 1)
    XChangeProperty(dpy,
                    c->win,
                    XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", false),
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (unsigned char *) &opacity,
                    1L);

  if (    XGetTransientForHint(dpy, w, &trans)
      && (t = wintoclient(trans)) )
  {
    c->mon  = t->mon;
    c->tags = t->tags;
  }
  else
  {
    c->mon = selmon;
    applyrules(c);
  }

  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldbw = wa->border_width;

  if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
    c->x = c->mon->mx + c->mon->mw - WIDTH(c);

  if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
    c->y = c->mon->my + c->mon->mh - HEIGHT(c);

  c->x = MAX(c->x, c->mon->mx);

  /* only fix client y-offset, if the client center might cover the bar */
  c->y = MAX(
    c->y,
    (   (c->mon->by == c->mon->my)
     && (c->x + (c->w / 2) >= c->mon->wx)
     && (c->x + (c->w / 2) <  c->mon->wx + c->mon->ww)
    ) ? bh : c->mon->my
  );

  c->bw = borderpx;

  if (c->iscentered || (c->mon->lt[c->mon->sellt]->arrange == NULL))
  {
    c->x = (c->mon->mw - WIDTH(c))  / 2;
    c->y = (c->mon->mh - HEIGHT(c)) / 2;
  }

  wc.border_width = c->bw;

  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, dc.norm[ColBorder].pixel);

  configure(c); /* propagates border_width, if size doesn't change */
  updatewindowtype(c);
  updatesizehints(c);
  updatewmhints(c);
  XSelectInput(dpy,
               w,
                 EnterWindowMask
               | FocusChangeMask
               | PropertyChangeMask
               | StructureNotifyMask);
  grabbuttons(c, false);

  if (!c->isfloating)
    c->isfloating = c->oldstate =
      ((trans != None) || c->isfixed);

  if (c->isfloating)
    XRaiseWindow(dpy, c->win);

  attach(c);
  attachstack(c);

  /* some windows require this */
  XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h);

  setclientstate(c, NormalState);

  if (c->mon == selmon)
    unfocus(selmon->sel, false);

  c->mon->sel = c;

#ifdef PWKL
  XkbGetState(dpy, XkbUseCoreKbd, &kbd_state);
  c->kbdgrp = kbd_state.group;
#endif
  arrange(c->mon);
  XMapWindow(dpy, c->win);
  focus(NULL);
}

static void
mappingnotify(XEvent *e)
{
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    grabkeys();
}

static void
maprequest(XEvent *e)
{
  static XWindowAttributes  wa;
  XMapRequestEvent         *ev = &e->xmaprequest;

  if (!XGetWindowAttributes(dpy, ev->window, &wa))
    return;

  if (wa.override_redirect)
    return;

  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

static void
monocle(Monitor *m)
{
  Client *c;
  for (c = nexttiled(m->clients);  c;  c = nexttiled(c->next))
  {
    resize(c,
           m->wx,
           m->wy,
           m->ww - 2 * c->bw,
           m->wh - 2 * c->bw,
           false);

    if (c->bw)
    {
      c->oldbw = c->bw;
      c->bw    = 0;
      resizeclient(c, m->wx, m->wy, m->ww, m->wh);
    }
  }
}

static void
motionnotify(XEvent *e)
{
  static Monitor *mon = NULL;
  Monitor        *m;
  XMotionEvent   *ev  = &e->xmotion;

  if (ev->window != root)
    return;

  if (  (m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon
      && mon )
  {
    selmon = m;
    focus(NULL);
  }

  mon = m;
}

static void
movemouse(__attribute__((unused)) const Arg *arg)
{
  int      x, y, ocx, ocy, nx, ny;
  Client  *c;
  Monitor *m;
  XEvent   ev;

  if (!(c = selmon->sel))
    return;

  restack(selmon);
  ocx = c->x;
  ocy = c->y;

  if (XGrabPointer(dpy,
                   root,
                   false,
                   MOUSEMASK,
                   GrabModeAsync,
                   GrabModeAsync,
                   None,
                   cursor[CurMove],
                   CurrentTime)
      != GrabSuccess)
    return;

  if (!getrootptr(&x, &y))
    return;

  do
  {
    XMaskEvent(dpy,
               MOUSEMASK | ExposureMask | SubstructureRedirectMask,
               &ev);

    switch (ev.type)
    {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      nx = ocx + (ev.xmotion.x - x);
      ny = ocy + (ev.xmotion.y - y);

      if (   nx >= selmon->wx
          && nx <= selmon->wx + selmon->ww
          && ny >= selmon->wy
          && ny <= selmon->wy + selmon->wh)
      {
        if (abs(selmon->wx - nx) < snap)
          nx = selmon->wx;

        else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c)))
                 < snap)
          nx = selmon->wx + selmon->ww - WIDTH(c);

        if (abs(selmon->wy - ny) < snap)
          ny = selmon->wy;

        else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c)))
                 < snap)
          ny = selmon->wy + selmon->wh - HEIGHT(c);

        if (   !c->isfloating
            &&  selmon->lt[ selmon->sellt ]->arrange
            && (   abs(nx - c->x) > snap
                || abs(ny - c->y) > snap ) )
          togglefloating(NULL);
      }

      if (!selmon->lt[ selmon->sellt ]->arrange || c->isfloating)
        resize(c, nx, ny, c->w, c->h, true);

      break;
    }
  } while (ev.type != ButtonRelease);

  XUngrabPointer(dpy, CurrentTime);

  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon)
  {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

static void
nametag(__attribute__((unused)) const Arg *arg)
{
  char *p, buf[256];
  FILE *f;
  int   i;

  snprintf(buf, sizeof(buf),
    "dmenu -p '%s' -fn '%s' -nb '%s' -nf '%s' -sb '%s' -sf '%s' "
    "</dev/null", "Current tag name: ",
    font, normbgcolor, normfgcolor, selbgcolor, selfgcolor);

  errno = 0; /* popen(3p) says on failure it "may" set errno */
  if (!(f = popen(buf, "r")))
  {
    fprintf(stderr, "dwm: nametag: Could not popen '%s'", buf);
    if (errno)
      fprintf(stderr, ": %s\n", strerror(errno));
    else
      fprintf(stderr, "\n");
    return;
  }

  if (!(p = fgets(buf, MAX_TAGNAMELEN, f)) && ferror(f))
    fprintf(stderr, "dwm: nametag: fgets failed: %s\n",
            strerror(errno));

  if (pclose(f) < 0)
    fprintf(stderr, "dwm: nametag: pclose failed: %s\n",
            strerror(errno));

  if (!p)
    return;

  if ((p = strchr(buf, '\n')))
    *p = '\0';

  for (i = 0; i < TAGS; i++)
    if (selmon->tagset[selmon->seltags] & (1 << i))
    {
      if (strlen(buf))
        sprintf(tags[selmon->num][i].tagname, "%i/%s", i+1, buf);
      else
        sprintf(tags[selmon->num][i].tagname, "%i",    i+1);
    }

  drawbar(selmon);
}

static Client *
nexttiled(Client *c)
{
  for (;  c && (c->isfloating || !ISVISIBLE(c));  c = c->next)
    /* NOTHING */;

  return c;
}

static void
pop(Client *c)
{
  detach(c);
  attach(c);
  focus(c);
  arrange(c->mon);
}

static void
propertynotify(XEvent *e)
{
  Client         *c;
  Window          trans;
  XPropertyEvent *ev = &e->xproperty;

  if ((ev->window == root) && (ev->atom == XA_WM_NAME))
    updatestatus();
  else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = wintoclient(ev->window)))
  {
    switch (ev->atom)
    {
    case XA_WM_TRANSIENT_FOR:
      if (   !c->isfloating
          &&  XGetTransientForHint(dpy, c->win, &trans)
          && (c->isfloating = (wintoclient(trans)) != NULL))
        arrange(c->mon);
      break;
    case XA_WM_NORMAL_HINTS:
      updatesizehints(c);
      break;
    case XA_WM_HINTS:
      updatewmhints(c);
      drawbars();
      if (c->isurgent)
        XSetWindowBorder(dpy, c->win, dc.sel[ColFG].pixel);
      break;
    default:
      break;
    }

    if (   ev->atom == XA_WM_NAME
        || ev->atom == netatom[NetWMName] )
    {
      updatetitle(c);
      if (c == c->mon->sel)
        drawbar(c->mon);
    }

    if (ev->atom == netatom[NetWMWindowType])
      updatewindowtype(c);
  }
}

static void
quit(const Arg *arg)
{
  if (arg->i)
    restart = true;

  running = false;

#ifdef SESSION_FILE
  if (restart)
    session_store();
#endif
}

static Monitor *
recttomon(int x, int y, int w, int h)
{
  int      area = 0;
  Monitor *m,
          *r    = selmon;

  for (m = mons;  m;  m = m->next)
  {
    int a;
    if ((a = INTERSECT(x, y, w, h, m)) > area)
    {
      area = a;
      r = m;
    }
  }

  return r;
}

static void
resize(Client *c, int x, int y, int w, int h, bool interact)
{
  if (applysizehints(c, &x, &y, &w, &h, interact))
    resizeclient(c, x, y, w, h);
}

static void
resizeclient(Client *c, int x, int y, int w, int h)
{
  XWindowChanges  wc;
  Client         *nbc;
  unsigned int    n;

  c->oldx = c->x;  c->x = wc.x      = x;
  c->oldy = c->y;  c->y = wc.y      = y;
  c->oldw = c->w;  c->w = wc.width  = w;
  c->oldh = c->h;  c->h = wc.height = h;

  /* Get number of clients for the selected monitor */
  for (n = 0, nbc = nexttiled(selmon->clients);
       nbc;
       nbc = nexttiled(nbc->next), n++)
    /* NOTHING */;

  /* Remove border if layout is monocle or only one client present */
  if (   selmon->lt[selmon->sellt]->arrange == monocle
      || n == 1 )
    wc.border_width = 0;
  else
    wc.border_width = c->bw;

  XConfigureWindow(dpy,
                   c->win,
                   CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
                   &wc);
  configure(c);
  XSync(dpy, false);
}

static void
resizemouse(__attribute__((unused)) const Arg *arg)
{
  int      ocx, ocy;
  int      nw,  nh;
  Client  *c;
  Monitor *m;
  XEvent   ev;

  if (!(c = selmon->sel))
    return;

  restack(selmon);
  ocx = c->x;
  ocy = c->y;

  if (XGrabPointer(dpy,
                   root,
                   false,
                   MOUSEMASK,
                   GrabModeAsync,
                   GrabModeAsync,
                   None,
                   cursor[CurResize],
                   CurrentTime)
      != GrabSuccess)
    return;

  XWarpPointer(dpy,
               None,
               c->win,
               0,
               0,
               0,
               0,
               c->w + c->bw - 1,
               c->h + c->bw - 1);

  do
  {
    XMaskEvent(dpy,
               MOUSEMASK | ExposureMask | SubstructureRedirectMask,
               &ev);

    switch(ev.type)
    {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
      nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);

      if (   c->mon->wx + nw >= selmon->wx
          && c->mon->wx + nw <= selmon->wx + selmon->ww
          && c->mon->wy + nh >= selmon->wy
          && c->mon->wy + nh <= selmon->wy + selmon->wh)
      {
        if (   !c->isfloating
            &&  selmon->lt[selmon->sellt]->arrange
            && (   abs(nw - c->w) > snap
                || abs(nh - c->h) > snap ) )
          togglefloating(NULL);
      }

      if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
        resize(c, c->x, c->y, nw, nh, true);

      break;
    }
  } while (ev.type != ButtonRelease);

  XWarpPointer(dpy,
               None,
               c->win,
               0,
               0,
               0,
               0,
               c->w + c->bw - 1,
               c->h + c->bw - 1);
  XUngrabPointer(dpy, CurrentTime);

  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    /* NOTHING */;

  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon)
  {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

static void
restack(Monitor *m)
{
  Client         *c;
  XEvent          ev;
  XWindowChanges  wc;

  drawbar(m);

  if (!m->sel)
    return;

  if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
    XRaiseWindow(dpy, m->sel->win);

  if (m->lt[m->sellt]->arrange)
  {
    wc.stack_mode = Below;
    wc.sibling    = m->barwin;

    for (c = m->stack; c; c = c->snext)
      if (!c->isfloating && ISVISIBLE(c))
      {
        XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
        wc.sibling = c->win;
      }
  }

  XSync(dpy, false);

  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    /* NOTHING */;
}

static void
run(void)
{
  XEvent ev;

  /* main event loop */
  XSync(dpy, false);
  while (running && !XNextEvent(dpy, &ev))
  {
    if (handler[ev.type])
      handler[ev.type](&ev); /* call handler */
  }
}

static void
scan(void)
{
  unsigned int      num;
  Window            d1, d2, *wins = NULL;
  XWindowAttributes wa;

  if (XQueryTree(dpy, root, &d1, &d2, &wins, &num))
  {
    for (unsigned int i = 0; i < num; i++)
    {
      if (   !XGetWindowAttributes(dpy, wins[i], &wa)
          ||  wa.override_redirect
          ||  XGetTransientForHint(dpy, wins[i], &d1))
        continue;

      if (   wa.map_state      == IsViewable
          || getstate(wins[i]) == IconicState )
        manage(wins[i], &wa);
    }

    for (unsigned int i = 0; i < num; i++)
    {
      /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
        continue;

      if (   XGetTransientForHint(dpy, wins[i], &d1)
          && (   wa.map_state      == IsViewable
              || getstate(wins[i]) == IconicState))
        manage(wins[i], &wa);
    }

    if (wins)
      XFree(wins);
  }
}

static void
sendmon(Client *c, Monitor *m)
{
  if (c->mon == m)
    return;

  unfocus(c, true);
  detach(c);
  detachstack(c);

  c->mon  = m;
  c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */

  attach(c);
  attachstack(c);
  focus(NULL);
  arrange(NULL);
}

#ifdef SESSION_FILE
static void
session_store(void)
{
  FILE *fw = fopen(SESSION_FILE, "w");

  /* get all the clients with their tags and write them to the file */
  for (Client *c = selmon->clients; c != NULL; c = c->next)
  {
    /* see `struct Client` for available data for saving */
    fprintf(fw, "%lu %u\n", c->win, c->tags);
  }

  fclose(fw);
}

static void
session_restore(void)
{
  FILE *fr = fopen(SESSION_FILE, "r");
  if (!fr)
    return;

  /* allocate enough space for excepted input from text file */
  char *str = malloc(23 * sizeof(char));

  /* read file till the end */
  while (fscanf(fr, "%[^\n] ", str) != EOF)
  {
    /* the read data must be the same as previously stored! */
    Window _win;
    long unsigned int _tags;

    /* get data: see session_store func */
    int data = sscanf(str, "%lu %lu", &_win, &_tags);

    /* break loop if data wasn't read correctly */
    if (data != 2)
      break;

    /* add tags to every window by _win (id) */
    for (Client *c = selmon->clients; c; c = c->next)
    {
      /* XXX (if necessary?) send to the right monitor using
       * `sendmon()`
       */
      if (c->win == _win)
      {
        c->tags = _tags;
        break;
      }
    }
  }

  /* refocus on windows */
  for (Client *c = selmon->clients; c; c = c->next)
  {
    focus(c);
    restack(c->mon);
  }

  /* rearrange all monitors */
  for (Monitor *m = selmon; m; m = m->next)
    arrange(m);

  free(str);
  fclose(fr);

  /* delete a session file */
  remove(SESSION_FILE);
}
#endif /* SESSION_FILE */


static void
setclientstate(Client *c, long state)
{
  long data[] = { state, None };

  XChangeProperty(dpy,
                  c->win,
                  wmatom[WMState],
                  wmatom[WMState],
                  32,
                  PropModeReplace,
                  (unsigned char *)data,
                  2);
}

static bool
sendevent(Client *c, Atom proto)
{
  int     n;
  Atom   *protocols;
  bool    exists = false;
  XEvent  ev;

  if (XGetWMProtocols(dpy, c->win, &protocols, &n))
  {
    while(!exists && n--)
      exists = protocols[n] == proto;
    XFree(protocols);
  }

  if (exists)
  {
    ev.type                 = ClientMessage;
    ev.xclient.window       = c->win;
    ev.xclient.message_type = wmatom[WMProtocols];
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = proto;
    ev.xclient.data.l[1]    = CurrentTime;

    XSendEvent(dpy, c->win, False, NoEventMask, &ev);
  }

  return exists;
}

static void
setfocus(Client *c)
{
  if (!c->neverfocus)
  {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy,
                    root,
                    netatom[NetActiveWindow],
                    XA_WINDOW,
                    32,
                    PropModeReplace,
                    (unsigned char *) &(c->win),
                    1);
  }

  sendevent(c, wmatom[WMTakeFocus]);
}

static void
setfullscreen(Client *c, bool fullscreen)
{
  if (fullscreen)
  {
    XChangeProperty(dpy,
                    c->win,
                    netatom[NetWMState],
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    (unsigned char*) &netatom[NetWMFullscreen],
                    1);

    c->isfullscreen = true;
    c->oldstate     = c->isfloating;
    c->oldbw        = c->bw;
    c->bw           = 0;
    c->isfloating   = true;

    resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
    XRaiseWindow(dpy, c->win);
  }
  else
  {
    XChangeProperty(dpy,
                    c->win,
                    netatom[NetWMState],
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    (unsigned char*) 0,
                    0);

    c->isfullscreen = false;
    c->isfloating   = c->oldstate;
    c->bw           = c->oldbw;
    c->x            = c->oldx;
    c->y            = c->oldy;
    c->w            = c->oldw;
    c->h            = c->oldh;

    resizeclient(c, c->x, c->y, c->w, c->h);
    arrange(c->mon);
  }
}

static void
setlayout(const Arg *arg)
{
  if (   !arg
      || !arg->v
      ||  arg->v != selmon->lt[selmon->sellt] )
  {
    selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
    selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
  }
  if (arg && arg->v)
    selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] =
      (Layout *)arg->v;

  selmon->lt[selmon->sellt] =
    selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];

  strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol,
          sizeof selmon->ltsymbol);

  if (selmon->sel)
    arrange(selmon);
  else
    drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutly */
static void
setmfact(const Arg *arg)
{
  float f;

  if (!arg || !selmon->lt[selmon->sellt]->arrange)
    return;

  f = (arg->f < 1.0)
    ? (arg->f + selmon->mfact)
    : (arg->f - 1.0);

  if (f < 0.1 || f > 0.9)
    return;

  selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
  arrange(selmon);
}

static void
setup(void)
{
  XSetWindowAttributes wa;

  /* clean up any zombies immediately */
  sigchld(0);

  signal(SIGHUP,  sighup);
  signal(SIGTERM, sigterm);

  /* init screen */
  screen  = DefaultScreen(dpy);
  root    = RootWindow(dpy, screen);

  initfont(font);

  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  bh = dc.h = dc.font.height + 2;

  updategeom();

  /* init atoms */
  wmatom[WMProtocols]            = XInternAtom(dpy, "WM_PROTOCOLS",      false);
  wmatom[WMDelete]               = XInternAtom(dpy, "WM_DELETE_WINDOW",  false);
  wmatom[WMState]                = XInternAtom(dpy, "WM_STATE",          false);
  wmatom[WMTakeFocus]            = XInternAtom(dpy, "WM_TAKE_FOCUS",     false);

  netatom[NetActiveWindow]       = XInternAtom(dpy, "_NET_ACTIVE_WINDOW",         false);
  netatom[NetSupported]          = XInternAtom(dpy, "_NET_SUPPORTED",             false);
  netatom[NetWMName]             = XInternAtom(dpy, "_NET_WM_NAME",               false);
  netatom[NetWMState]            = XInternAtom(dpy, "_NET_WM_STATE",              false);
  netatom[NetWMFullscreen]       = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN",   false);
  netatom[NetWMWindowType]       = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE",        false);
  netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", false);

  /* init cursors */
  cursor[CurNormal]  = XCreateFontCursor(dpy, XC_left_ptr);
  cursor[CurResize]  = XCreateFontCursor(dpy, XC_sizing);
  cursor[CurMove]    = XCreateFontCursor(dpy, XC_fleur);

  /* init appearance */
  dc.norm[ColBorder] = getcolor(normbordercolor);
  dc.norm[ColBG]     = getcolor(normbgcolor);
  dc.norm[ColFG]     = getcolor(normfgcolor);
  dc.sel[ColBorder]  = getcolor(selbordercolor);
  dc.sel[ColBG]      = getcolor(selbgcolor);
  dc.sel[ColFG]      = getcolor(selfgcolor);
  dc.drawable        = XCreatePixmap(dpy,
                                     root,
                                     DisplayWidth(dpy, screen),
                                     bh,
                                     DefaultDepth(dpy, screen));
  dc.gc              = XCreateGC(dpy, root, 0, NULL);

  XSetLineAttributes(dpy, dc.gc, 1, LineSolid, CapButt, JoinMiter);

  /* init bars */
  updatebars();
  updatestatus();

  /* EWMH support per view */
  XChangeProperty(dpy,
                  root,
                  netatom[NetSupported],
                  XA_ATOM,
                  32,
                  PropModeReplace,
                  (unsigned char *) netatom,
                  NetLast);

  /* select for events */
  wa.cursor     = cursor[CurNormal];
  wa.event_mask = SubstructureRedirectMask
                | SubstructureNotifyMask
                | ButtonPressMask
                | PointerMotionMask
                | EnterWindowMask
                | LeaveWindowMask
                | StructureNotifyMask
                | PropertyChangeMask;

  XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  grabkeys();
}

static void
showhide(Client *c)
{
  if (!c)
    return;

  if (ISVISIBLE(c))
  {
    /* show clients top down */
    XMoveWindow(dpy, c->win, c->x, c->y);

    if (  (  !c->mon->lt[c->mon->sellt]->arrange
           || c->isfloating )
        && !c->isfullscreen )
      resize(c, c->x, c->y, c->w, c->h, false);

    showhide(c->snext);
  }
  else
  {
    /* hide clients bottom up */
    showhide(c->snext);
    XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
  }
}

static void
sigchld(__attribute__((unused)) int sig)
{
  if (signal(SIGCHLD, sigchld) == SIG_ERR)
    die("Can't install SIGCHLD handler");

  while (0 < waitpid(-1, NULL, WNOHANG))
    /* NOTHING */;
}

static void
sighup(__attribute__((unused)) int sig)
{
  Arg a = {.i = 1};
  quit(&a);
}

static void
sigterm(__attribute__((unused)) int sig)
{
  Arg a = {.i = 0};
  quit(&a);
}

static void
spawn(const Arg *arg)
{
  if (fork() == 0)
  {
    if (dpy)
      close(ConnectionNumber(dpy));

    setsid();
    execvp(((char **)arg->v)[0],
            (char **)arg->v);
    fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
    perror(" failed");
    exit(EXIT_SUCCESS);
  }
}

static void
tag(const Arg *arg)
{
  if (   selmon->sel
      && arg->ui & TAGMASK)
  {
    selmon->sel->tags = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selmon);
  }
}

static void
tagmon(const Arg *arg)
{
  if (!selmon->sel || !mons->next)
    return;

  sendmon(selmon->sel, dirtomon(arg->i));
}

static int
textnw(const char *text, unsigned int len)
{
  XGlyphInfo ext;

  XftTextExtentsUtf8(dpy, dc.font.xfont, (XftChar8 *)text, len, &ext);
  return ext.xOff;
}

static void
tile(Monitor *m)
{
  int      i, n, h, mw, my, ty;
  Client  *c;

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), n++)
    /* NOTHING */;

  if (n == 0)
    return;

  if (n > m->nmaster)
    mw = m->nmaster ? (m->ww * m->mfact) : 0;
  else
    mw = m->ww;

  for (i = my = ty = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), i++)
  {
    if (i < m->nmaster)
    {
      h = (m->wh - my) / (MIN(n, m->nmaster) - i);
      resize(c,
             m->wx,
             m->wy + my,
             mw - (2 * c->bw),
             h  - (2 * c->bw),
             false);
      my += HEIGHT(c);
    }
    else
    {
      h = (m->wh - ty) / (n - i);
      resize(c,
             m->wx + mw,
             m->wy + ty,
             m->ww - mw - (2 * c->bw),
                      h - (2 * c->bw),
             false);
      ty += HEIGHT(c);
    }
  }
}

static void
togglebar(__attribute__((unused)) const Arg *arg)
{
  selmon->showbar =
    selmon->pertag->showbars[selmon->pertag->curtag] =
      !selmon->showbar;

  updatebarpos(selmon);
  XMoveResizeWindow(dpy,
                    selmon->barwin,
                    selmon->wx,
                    selmon->by,
                    selmon->ww,
                    bh);
  arrange(selmon);
}

static void
togglefloating(__attribute__((unused)) const Arg *arg)
{
  if (!selmon->sel)
    return;

  selmon->sel->isfloating =
    !selmon->sel->isfloating || selmon->sel->isfixed;

  if (selmon->sel->isfloating)
  {
    if (selmon->sel->bw != borderpx)
    {
      selmon->sel->oldbw = selmon->sel->bw;
      selmon->sel->bw    = borderpx;
    }
    resize(selmon->sel,
           selmon->sel->x,
           selmon->sel->y,
           selmon->sel->w - selmon->sel->bw * 2,
           selmon->sel->h - selmon->sel->bw * 2,
           false);
  }

  arrange(selmon);
}

static void
togglefullscr(__attribute__((unused)) const Arg *arg)
{
  if (selmon->sel)
    setfullscreen(selmon->sel, !selmon->sel->isfullscreen);
}

static void
toggletag(const Arg *arg)
{
  unsigned int newtags;

  if(!selmon->sel)
    return;

  if ((newtags = selmon->sel->tags ^ (arg->ui & TAGMASK)))
  {
    selmon->sel->tags = newtags;
    focus(NULL);
    arrange(selmon);
  }
}

static void
toggleview(const Arg *arg)
{
  int newtagset =
    selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

  if (!newtagset)
    return;

  if (newtagset == ~0)
  {
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->pertag->curtag  = 0;
  }
  /* test if the user did not select the same tag */
  if (!(newtagset & 1 << (selmon->pertag->curtag - 1)))
  {
    selmon->pertag->prevtag = selmon->pertag->curtag;
    size_t i = 0;

    for (; !(newtagset & 1 << i); i++)
      /* NOTHING */;

    selmon->pertag->curtag = i + 1;
  }
  selmon->tagset[selmon->seltags] = newtagset;

  /* apply settings for this view */
  selmon->nmaster             = selmon->pertag->nmasters[selmon->pertag->curtag];
  selmon->mfact               = selmon->pertag->mfacts[selmon->pertag->curtag];
  selmon->sellt               = selmon->pertag->sellts[selmon->pertag->curtag];
  selmon->lt[selmon->sellt]   = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
  selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];

  if (   selmon->showbar
      != selmon->pertag->showbars[selmon->pertag->curtag] )
    togglebar(NULL);

  focus(NULL);
  arrange(selmon);
}

static void
unfocus(Client *c, bool setfocus)
{
  if (!c)
    return;

#ifdef PWKL
  XkbStateRec kbd_state;
#endif

  grabbuttons(c, false);
  XSetWindowBorder(dpy, c->win, dc.norm[ColBorder].pixel);

  if (setfocus)
  {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
#ifdef PWKL
  XkbGetState(dpy, XkbUseCoreKbd, &kbd_state);
  c->kbdgrp = kbd_state.group;
#endif
}

static void
unmanage(Client *c, bool destroyed)
{
  Monitor        *m = c->mon;
  XWindowChanges  wc;

  /* The server grab construct avoids race conditions. */
  detach(c);
  detachstack(c);
  if (!destroyed)
  {
    wc.border_width = c->oldbw;
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);

    /* restore border */
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc);

    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(dpy, false);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
  free(c);
  focus(NULL);
  arrange(m);
}

static void
unmapnotify(XEvent *e)
{
  Client      *c;
  XUnmapEvent *ev = &e->xunmap;

  if ((c = wintoclient(ev->window)))
  {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, false);
  }
}

static void
updatebars(void)
{
  Monitor *m;

  XSetWindowAttributes wa = {
    .override_redirect  = true,
    .background_pixmap  = ParentRelative,
    .event_mask         = ButtonPressMask | ExposureMask
  };

  for (m = mons; m; m = m->next)
  {
    m->barwin =
      XCreateWindow(dpy,
                    root,
                    m->wx,
                    m->by,
                    m->ww,
                    bh,
                    0,
                    DefaultDepth(dpy, screen),
                    CopyFromParent,
                    DefaultVisual(dpy, screen),
                    CWOverrideRedirect | CWBackPixmap | CWEventMask,
                    &wa);

    XDefineCursor(dpy, m->barwin, cursor[CurNormal]);
    XMapRaised(dpy, m->barwin);
    XChangeProperty(dpy,
                    m->barwin,
                    XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", false),
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (unsigned char *) &opacity,
                    1L);
  }
}

static void
updatebarpos(Monitor *m)
{
  m->wy = m->my;
  m->wh = m->mh;
  if (m->showbar)
  {
    m->wh -= bh;
    m->by  = m->topbar ?  m->wy       : (m->wy + m->wh);
    m->wy  = m->topbar ? (m->wy + bh) :  m->wy;
  }
  else
    m->by  = -bh;
}

static bool
updategeom(void)
{
  bool dirty = false;

#ifdef XINERAMA
  if (XineramaIsActive(dpy))
  {
    int                 i, j, n, nn;
    Client             *c;
    Monitor            *m;
    XineramaScreenInfo *info   = XineramaQueryScreens(dpy, &nn);
    XineramaScreenInfo *unique = NULL;

    for (n = 0, m = mons; m; m = m->next, n++)
      /* NOTHING */;

    /* only consider unique geometries as separate screens */
    if (!(unique =
          (XineramaScreenInfo *)
            malloc(sizeof(XineramaScreenInfo) * nn)))
      die("fatal: could not malloc() %u bytes\n",
          sizeof(XineramaScreenInfo) * nn);

    for (i = 0, j = 0; i < nn; i++)
    {
      if (isuniquegeom(unique, j, &info[i]))
        memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    }

    XFree(info);

    nn = j;
    if (n <= nn)
    {
      for (i = 0; i < (nn - n); i++)
      { /* new monitors available */
        for (m = mons;  m && m->next;  m = m->next)
          /* NOTHING */;
        if (m)
          m->next = createmon();
        else
          mons    = createmon();
      }

      for (i = 0, m = mons;  i < nn && m;  m = m->next, i++)
        if (i >= n
            || (   unique[i].x_org  != m->mx
                || unique[i].y_org  != m->my
                || unique[i].width  != m->mw
                || unique[i].height != m->mh ))
        {
          dirty  = true;
          m->num = i;
          m->mx  = m->wx = unique[i].x_org;
          m->my  = m->wy = unique[i].y_org;
          m->mw  = m->ww = unique[i].width;
          m->mh  = m->wh = unique[i].height;
          updatebarpos(m);
        }
    }
    else
    { /* less monitors available nn < n */
      for (i = nn; i < n; i++)
      {
        for (m = mons;  m && m->next;  m = m->next)
          /* NOTHING */;

        while (m->clients)
        {
          dirty      = true;
          c          = m->clients;
          m->clients = c->next;
          detachstack(c);
          c->mon     = mons;
          attach(c);
          attachstack(c);
        }

        if (m == selmon)
          selmon = mons;

        cleanupmon(m);
      }
    }
    free(unique);
  }
  else
#endif  /* XINERAMA */

  /* default monitor setup */
  {
    if (!mons)
      mons = createmon();

    if (   mons->mw != sw
        || mons->mh != sh )
    {
      dirty    = true;
      mons->mw = mons->ww = sw;
      mons->mh = mons->wh = sh;
      updatebarpos(mons);
    }
  }
  if (dirty)
  {
    selmon = mons;
    selmon = wintomon(root);
  }
  return dirty;
}

static void
updatenumlockmask(void)
{
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap      = XGetModifierMapping(dpy);
  for (int i = 0; i < 8; i++)
  {
    for (int j = 0; j < modmap->max_keypermod; j++)
    {
      if (modmap->modifiermap[i * modmap->max_keypermod + j]
          == XKeysymToKeycode(dpy, XK_Num_Lock))
        numlockmask = (1 << i);
    }
  }

  XFreeModifiermap(modmap);
}

static void
updatesizehints(Client *c)
{
  long       msize;
  XSizeHints size;

  if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
  {
    /* size is uninitialized, ensure that size.flags aren't used */
    size.flags = PSize;
  }

  if (size.flags & PBaseSize)
  {
    c->basew = size.base_width;
    c->baseh = size.base_height;
  }
  else if (size.flags & PMinSize)
  {
    c->basew = size.min_width;
    c->baseh = size.min_height;
  }
  else
    c->basew = c->baseh = 0;

  if (size.flags & PResizeInc)
  {
    c->incw  = size.width_inc;
    c->inch  = size.height_inc;
  }
  else
    c->incw  = c->inch = 0;

  if (size.flags & PMaxSize)
  {
    c->maxw  = size.max_width;
    c->maxh  = size.max_height;
  }
  else
    c->maxw  = c->maxh = 0;

  if (size.flags & PMinSize)
  {
    c->minw  = size.min_width;
    c->minh  = size.min_height;
  }
  else if (size.flags & PBaseSize)
  {
    c->minw  = size.base_width;
    c->minh  = size.base_height;
  }
  else
    c->minw  = c->minh = 0;

  if (size.flags & PAspect)
  {
    c->mina  = (float)size.min_aspect.y / size.min_aspect.x;
    c->maxa  = (float)size.max_aspect.x / size.max_aspect.y;
  }
  else
    c->maxa  = c->mina = 0.0;

  c->isfixed = (    c->maxw
                &&  c->minw
                &&  c->maxh
                &&  c->minh
                && (c->maxw == c->minw)
                && (c->maxh == c->minh) );
}

static void
updatetitle(Client *c)
{
  if (!gettextprop(c->win,
                   netatom[NetWMName],
                          c->name,
                   sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);

  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

static void
updatestatus(void)
{
  if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
    strcpy(stext, "dwm "VERSION);

  drawbar(selmon);
}

static void
updatewindowtype(Client *c)
{
  Atom state = getatomprop(c, netatom[NetWMState]);
  Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

  if (state == netatom[NetWMFullscreen])
    setfullscreen(c, true);

  if (wtype == netatom[NetWMWindowTypeDialog])
    c->isfloating = true;
}

static void
updatewmhints(Client *c)
{
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win)))
  {
    if (   c == selmon->sel
        && wmh->flags & XUrgencyHint )
    {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
    }
    else
      c->isurgent = (wmh->flags & XUrgencyHint) ? true : false;

    c->neverfocus = (wmh->flags & InputHint) ? (!wmh->input) : false;
    XFree(wmh);
  }
}

static void
view(const Arg *arg)
{
  if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
    return;

  selmon->seltags ^= 1; /* toggle sel tagset */

  if (arg->ui & TAGMASK)
  {
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;

    if (arg->ui == ~0)
      selmon->pertag->curtag = 0;
    else
    {
      size_t i;
      for (i = 0; !(arg->ui & 1 << i); i++)
        /* NOTHING */;
      selmon->pertag->curtag = i + 1;
    }
  }
  else
  {
    unsigned int tmptag     = selmon->pertag->prevtag;
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->pertag->curtag  = tmptag;
  }

  selmon->nmaster             = selmon->pertag->nmasters[selmon->pertag->curtag];
  selmon->mfact               = selmon->pertag->mfacts[  selmon->pertag->curtag];
  selmon->sellt               = selmon->pertag->sellts[  selmon->pertag->curtag];
  selmon->lt[selmon->sellt]   = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
  selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];

  if (   selmon->showbar
      != selmon->pertag->showbars[selmon->pertag->curtag])
    togglebar(NULL);

  focus(NULL);
  arrange(selmon);
}

static Client *
wintoclient(Window w)
{
  Client  *c;
  Monitor *m;

  for (m = mons; m; m = m->next)
  {
    for (c = m->clients; c; c = c->next)
    {
      if (c->win == w)
        return c;
    }
  }

  return NULL;
}

static Monitor *
wintomon(Window w)
{
  int      x, y;
  Client  *c;
  Monitor *m;

  if (w == root && getrootptr(&x, &y))
    return recttomon(x, y, 1, 1);

  for (m = mons; m; m = m->next)
  {
    if (w == m->barwin)
      return m;
  }

  if ((c = wintoclient(w)))
    return c->mon;

  return selmon;
}

/* Selects for the view of the focused window.  The list of tags
 * to be displayed is matched to the focused window tag list. */
void
winview(__attribute__((unused)) const Arg* arg)
{
  Window    win, win_r, win_p, *win_c;
  unsigned  nc;
  int       unused;
  Client   *c;
  Arg       a;

  if (!XGetInputFocus(dpy, &win, &unused))
    return;

  while (   XQueryTree(dpy, win, &win_r, &win_p, &win_c, &nc)
         && win_p != win_r)
    win = win_p;

  if (!(c = wintoclient(win)))
    return;

  a.ui = c->tags;
  view(&a);
}

/* There's no way to check accesses to destroyed windows, thus those
 * cases are ignored (especially on UnmapNotify's).  Other types of
 * errors call Xlib's default error handler, which may call exit. */
static int
xerror(Display *dpy, XErrorEvent *ee)
{
  if (       ee->error_code   == BadWindow
      || (   ee->request_code == X_SetInputFocus
          && ee->error_code   == BadMatch)
      || (   ee->request_code == X_PolyText8
          && ee->error_code   == BadDrawable)
      || (   ee->request_code == X_PolyFillRectangle
          && ee->error_code   == BadDrawable)
      || (   ee->request_code == X_PolySegment
          && ee->error_code   == BadDrawable)
      || (   ee->request_code == X_ConfigureWindow
          && ee->error_code   == BadMatch)
      || (   ee->request_code == X_GrabButton
          && ee->error_code   == BadAccess)
      || (   ee->request_code == X_GrabKey
          && ee->error_code   == BadAccess)
      || (   ee->request_code == X_CopyArea
          && ee->error_code   == BadDrawable))
    return 0;

  fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
          ee->request_code,
          ee->error_code);

  return xerrorxlib(dpy, ee); /* may call exit */
}

static int
xerrordummy(__attribute__((unused)) Display     *dpy,
            __attribute__((unused)) XErrorEvent *ee )
{
  return 0;
}

/* Startup Error handler to check if another window manager is already
 * running. */
static int
xerrorstart(__attribute__((unused)) Display     *dpy,
            __attribute__((unused)) XErrorEvent *ee )
{
  die("dwm: another window manager is already running\n");
  return -1;
}

static void
zoom(__attribute__((unused)) const Arg *arg)
{
  Client *c = selmon->sel;

  if (   !selmon->lt[selmon->sellt]->arrange
      || (   selmon->sel
          && selmon->sel->isfloating ))
    return;

  if (c == nexttiled(selmon->clients))
    if (!c || !(c = nexttiled(c->next)))
      return;

  pop(c);
}

int
main(int argc, char *argv[])
{
  if (argc == 2 && !strcmp("-v", argv[1]))
    die("dwm "VERSION"\n");
  else if (argc != 1)
    die("usage: dwm [-v]\n");

  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    fputs("warning: no locale support\n", stderr);

  if (!(dpy = XOpenDisplay(NULL)))
    die("dwm: cannot open display\n");

  checkotherwm();
  setup();
  scan();
#ifdef SESSION_FILE
  session_restore();
#endif
  run();
  if (restart)
    execvp(argv[0], argv);
  cleanup();
  XCloseDisplay(dpy);

  return EXIT_SUCCESS;
}

/* vim:sw=2:ts=2:sts=2:et:cc=72:tw=70
 * End of file. */
