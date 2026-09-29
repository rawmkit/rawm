/* See LICENSE file for copyright and license details. */

/* dynamic window manager is designed like any other X client as well.
 * It is driven through handling X events.  In contrast to other X
 * clients, a window manager selects for SubstructureRedirectMask on
 * the root window, to receive events about window (dis-)appearance.
 * Only one X connection at a time is allowed to select for this event
 * mask.
 *
 * The event handlers of rawm are organized in an array which is
 * accessed whenever a new event has been fetched.  This allows event
 * dispatching in O(1) time.
 *
 * Ordinary children of the root window become clients. Windows with
 * override_redirect set and EWMH dock windows remain external surfaces.
 * Clients are organized
 * in a linked client list on each monitor, the focus history is
 * remembered through a stack list on each monitor.  Each client
 * contains a bit array to indicate the tags of a client.
 *
 * Window rules, layout, appearance, and pointer policy are loaded from rawm.conf.
 *
 * To understand everything else, start reading main().
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#include "rawm-config.h"
#include "rawm-control.h"

/* Xinerama support for multiple monitors. */
#ifdef XINERAMA
# include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

/*********************************************************************
 * Macros.
 */

/** Button mask for grabbing mouse events. */
#define BUTTONMASK  (ButtonPressMask | ButtonReleaseMask)

/**
 * @brief Clean modifier mask from NumLock and LockMask.
 * @param mask The modifier mask to clean.
 */
#define CLEANMASK(mask)                                              \
  (    mask                                                          \
   & ~(numlockmask | LockMask)                                       \
   &  (ShiftMask   | ControlMask |                                   \
       Mod1Mask    | Mod2Mask    | Mod3Mask | Mod4Mask | Mod5Mask))

/**
 * @brief Calculate the area of intersection between a rectangle and a monitor's work area.
 * @param x X-coordinate of the rectangle.
 * @param y Y-coordinate of the rectangle.
 * @param w Width of the rectangle.
 * @param h Height of the rectangle.
 * @param m Pointer to the Monitor structure.
 * @return The intersecting area.
 */
#define INTERSECT(x, y, w, h, m) \
  (  MAX(0, MIN((x) + (w), (m)->wx + (m)->ww)  -  MAX((x), (m)->wx)) \
   * MAX(0, MIN((y) + (h), (m)->wy + (m)->wh)  -  MAX((y), (m)->wy)) )

/**
 * @brief Check if a client is visible on the currently selected tags of its monitor.
 * @param C Pointer to the Client structure.
 * @return True if visible, false otherwise.
 */
#define ISVISIBLE(C)  ((C->tags & C->mon->tagset[C->mon->seltags]))

/**
 * @brief Calculate the number of elements in an array.
 * @param X The array.
 */
#define LENGTH(X)  (sizeof X / sizeof X[0])

/**
 * @brief Maximum of two values.
 * @param A First value.
 * @param B Second value.
 * @return The maximum value.
 */
#define MAX(A, B)  ((A) > (B) ? (A) : (B))

/**
 * @brief Minimum of two values.
 * @param A First value.
 * @param B Second value.
 * @return The minimum value.
 */
#define MIN(A, B)  ((A) < (B) ? (A) : (B))

/** Maximum number of colors used for drawing. */
#define MAXCOLORS  RAWM_COLOR_SCHEMES
#define NUMCOLORS  RAWM_COLOR_SCHEMES

/** Mouse mask for grabbing pointer motion and button events */
#define MOUSEMASK  (BUTTONMASK|PointerMotionMask)

/**
 * @brief Calculate the total width of a client including border.
 * @param X Pointer to the Client structure.
 * @return The total width.
 */
#define WIDTH(X)   ((X)->w + 2 * (X)->bw)

/**
 * @brief Calculate the total height of a client including border.
 * @param X Pointer to the Client structure.
 * @return The total height.
 */
#define HEIGHT(X)  ((X)->h + 2 * (X)->bw)

/** Mask for all configured tags. */
#define TAGMASK    (config.tag_count == 31 ? 0x7fffffffu : ((1u << config.tag_count) - 1u))

/**
 * Runtime tag label capacity:
 * two-digit tag number + '/' + configured tag name + terminating NUL.
 */
#define MAX_TAGLEN (RAWM_TAG_NAME_MAX + 4u)

/**
 * Runtime layout label capacity:
 * configured symbol plus rawm-owned dynamic counter suffixes.
 */
/*********************************************************************
 * Enums & Typedefs.
 */

/** Cursor types used in rawm. */
enum {
  CurNormal, /**< Normal pointer cursor. */
  CurResize, /**< Resize cursor. */
  CurMove,   /**< Move cursor. */
  CurLast    /**< Sentinel value for the last cursor type. */
};

/** Color definitions for drawing. */
enum {
  ColBorder,
  ColLast
};

/** Configured color schemes. */
enum {
  SchemeNorm,
  SchemeSel,
  SchemeUrg,
  SchemeFloating
};

/** EWMH atoms used for window properties and communication. */
enum {
  NetSupported,             /**< _NET_SUPPORTED atom. */
  NetWMName,                /**< _NET_WM_NAME atom. */
  NetWMState,               /**< _NET_WM_STATE atom. */
  NetWMFullscreen,          /**< _NET_WM_STATE_FULLSCREEN atom. */
  NetActiveWindow,          /**< _NET_ACTIVE_WINDOW atom. */
  NetClientList,            /**< _NET_CLIENT_LIST atom. */
  NetWMWindowType,          /**< _NET_WM_WINDOW_TYPE atom. */
  NetWMWindowTypeDialog,    /**< _NET_WM_WINDOW_TYPE_DIALOG atom. */
  NetWMStrut,               /**< _NET_WM_STRUT atom. */
  NetWMStrutPartial,        /**< _NET_WM_STRUT_PARTIAL atom. */
  NetLast                   /**< Sentinel value for the last EWMH atom. */
};

/** Default WM atoms used for window protocols and state. */
enum {
  WMProtocols, /**< WM_PROTOCOLS atom. */
  WMDelete,    /**< WM_DELETE_WINDOW atom. */
  WMState,     /**< WM_STATE atom. */
  WMTakeFocus, /**< WM_TAKE_FOCUS atom. */
  WMLast       /**< Sentinel value for the last WM atom. */
};

/** rawm-private client properties for compositor integration. */
enum {
  RawmAtomFloating, /**< _RAWM_FLOATING effective presentation plane. */
  RawmAtomSelected, /**< _RAWM_SELECTED global selection state. */
  RawmAtomLayout,   /**< _RAWM_LAYOUT stable layout name. */
  RawmAtomMonitor,  /**< _RAWM_MONITOR monitor number. */
  RawmAtomLast      /**< Sentinel value for the last rawm-private atom. */
};

/** Client click area for pointer bindings. */
enum {
  ClkClientWin,
  ClkRootWin,
  ClkLast
};

/** Argument union for key/button bindings. */
typedef union {
 int i;           /**< Integer argument. */
 unsigned int ui; /**< Unsigned integer argument. */
 float f;         /**< Float argument. */
 const void *v;   /**< Pointer argument. */
} Arg;

/** Button definition for mouse bindings. */
typedef struct {
  unsigned int mask;            /**< Modifier mask (e.g., MODKEY). */
  unsigned int button;          /**< Mouse button (e.g., Button1). */
  void (*func)(const Arg *arg); /**< Function to execute. */
  Arg arg;                      /**< Argument for the function. */
} Button;

/* Forward declarations. */
typedef struct Monitor  Monitor;
typedef struct Client   Client;

typedef struct {
  int x, y, w, h;
} RawmGeometry;

typedef enum {
  RAWM_POSITION_UNSPECIFIED,
  RAWM_POSITION_PROGRAM,
  RAWM_POSITION_USER
} RawmPositionAuthority;

typedef struct {
  unsigned int viewed;
  unsigned int occupied;
  unsigned int urgent;
} RawmTagState;

/**
 * @brief Client structure to manage individual windows.
 */
struct Client {
  char name[256];       /**< Window title. */
  char class_name[256]; /**< WM_CLASS class used for admitted-client metadata. */
  float mina, maxa;     /**< Min/max aspect ratios from size hints. */
  int x, y, w, h;       /**< Current geometry (position and size). */
  int oldx, oldy, oldw, oldh; /**< Previous geometry. */
  RawmGeometry freegeom; /**< Last geometry owned outside an arranger. */
  bool freegeom_valid;   /**< Whether freegeom contains admitted state. */
  int basew, baseh, incw, inch, maxw, maxh, minw, minh; /**< Size hints. */
  int bw, oldbw;        /**< Current and previous border width. */
  unsigned int tags;    /**< Tag mask indicating which tags the client is on. */
  bool isfixed, isfloating, isdialog, istransient, center_explicit, app_urgent, wm_urgent, neverfocus, oldstate, isfullscreen; /**< Various state flags. */
  RawmCenterMode center; /**< Placement policy for centered windows. */
  RawmPositionAuthority position_authority; /**< Initial client position authority. */
  Client *next;         /**< Next client in the client list for the monitor. */
  Client *snext;        /**< Next client in the stack list for the monitor (focus history). */
  Monitor *mon;         /**< Pointer to the monitor the client is on. */
  Window win;           /**< X window ID. */
};

/**
 * @brief Drawing context for window-manager border colors.
 */
typedef struct {
  unsigned long colors[MAXCOLORS][ColLast];
  GC gc;
} DC;

/**
 * @brief Layout definition.
 *
 * Defines a layout symbol and the corresponding arrangement function.
 */
typedef struct {
  void (*arrange)(Monitor *); /**< Function to arrange clients on a monitor (e.g., tile, monocle). NULL means floating. */
} Layout;

/* Forware declaration for Pertag structure. */
typedef struct Pertag Pertag;

/**
 * @brief Monitor structure to manage screens.
 */
struct Monitor {
  char tag_names[RAWM_MAX_TAGS][MAX_TAGLEN]; /**< Runtime tag labels. */
  float mfact;            /**< Master area factor [0.05..0.95] (per tag). */
  int nmaster;            /**< Number of clients in master area (per tag). */
  int num;                /**< Monitor number (0, 1, ...). */
  int mx, my, mw, mh;     /**< Monitor geometry (full screen size). */
  int wx, wy, ww, wh;     /**< Work area geometry after external dock reservations. */
  unsigned int seltags;   /**< Index of the currently selected tagset (0 or 1). */
  unsigned int sellt;     /**< Index of the currently selected layout (0 or 1). */
  unsigned int tagset[2]; /**< Array holding current and previous tag masks. */
  Client *clients;        /**< List of clients on this monitor. */
  Client *sel;            /**< Currently selected client on this monitor. */
  Client *stack;          /**< Stacked list of clients on this monitor (focus history). */
  Monitor *next;          /**< Next monitor in the global monitor list. */
  const Layout *lt[2];    /**< Array holding current and previous layout (per tag). */
  Pertag *pertag;         /**< Pointer to the per-tag configuration for this monitor. */
};

/*********************************************************************
 * Function declarations.
 */

static void           activateclient(Client *c);
static void           applyrules(Client *c,
                                 bool *has_fullscreen,
                                 bool *fullscreen,
                                 bool *has_attention,
                                 RawmAttentionMode *attention);
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
static void           centerclient(Client *c, RawmCenterMode mode);
static void           clearurgent(Client *c);
static bool           clienturgent(const Client *c);
static uint32_t       clientmessagecard32(const XClientMessageEvent *message,
                                          int index);
static void           clientmessage(XEvent *e);
static bool           controlargumenttag(int32_t selector, Arg *arg, bool allow_previous);
static void           handlecontrolcommand(const RawmControlRequest *request);
static int            layoutid(const Layout *layout);
static RawmTagState   monitortagstate(const Monitor *m);
static void           publishcontrolstate(void);
static void           publishfeedback(const char *event, const Monitor *m,
                                      int32_t arg0, int32_t arg1);
static void           publishcontrolclients(void);
static void           configure(Client *c);
static void           configurenotify(XEvent *e);
static void           configurerequest(XEvent *e);
static void           cyclelayout(const Arg *arg);
static void           cycleview(const Arg *arg);
static Monitor       *createmon(int idx);
static void           destroynotify(XEvent *e);
static void           detach(Client *c);
static void           detachstack(Client *c);
static void           die(const char *errstr, ...);
static Monitor       *dirtomon(int dir);
static void           enternotify(XEvent *e);
static void           focus(Client *c);
static void           focusin(XEvent *e);
static void           focusmon(const Arg *arg);
static void           focusnstack(const Arg *arg);
static void           focusstack(const Arg *arg);
static void           grid(Monitor *m);
static bool           atomlistcontains(Window w, Atom prop, Atom atom);
static bool           getdockstrut(Window w, unsigned long strut[12]);
static void           setatomlistmember(Window w, Atom prop, Atom atom, bool present);
static bool           clientiselevated(const Client *c);
static int            clientselectedscheme(const Client *c);
static void           clearclientcomposition(Client *c);
static void           updateclientcomposition(Client *c);


static unsigned long  getcolor(const char *colstr);
static bool           getrootptr(int *x, int *y);
static Atom           getatomprop(Client *c, Atom prop);
static long           getstate(Window w);
static bool           gettextprop(Window w, Atom atom, char *text,
                                  unsigned int size);
static void           grabbuttons(Client *c, bool focused);
static void           incnmaster(const Arg *arg);
static void           killclient(const Arg *arg);
static void           manage(Window w, XWindowAttributes *wa);
static void           mappingnotify(XEvent *e);
static void           mapnotify(XEvent *e);
static void           maprequest(XEvent *e);
static void           monocle(Monitor *m);
static void           motionnotify(XEvent *e);
static void           movemouse(const Arg *arg);
static void           settagname(const char *name);
static Client        *nexttiled(Client *c);
static void           pop(Client *);
static void           propertynotify(XEvent *e);
static void           quit(const Arg *arg);
static Monitor       *recttomon(int x, int y, int w, int h);
static void           resize(Client *c, int x, int y, int w, int h,
                             bool interact);
static void           resizeclient(Client *c, int x, int y, int w, int h);
static bool           clientisfree(const Client *c);
static int            clientdisplayx(const Client *c);
static void           placefreeclient(Client *c);
static void           savefreegeometry(Client *c);
static void           restorefreegeometry(Client *c);
static void           resizemouse(const Arg *arg);
static void           restack(Monitor *m);
static void           run(void);
static void           scan(void);

static bool           sendevent(Client *c, Atom proto);

static void           sendmon(Client *c, Monitor *m);
static void           setclientstate(Client *c, long state);
static void           setfocus(Client *c);
static void           setfullscreen(Client *c, bool fullscreen);
static void           setlayout(const Arg *arg);
static void           setmfact(const Arg *arg);
static void           setup(void);
static void           showhide(Client *c);
static int            setsignalpipeflags(int fd);
static int            installsignalhandler(int signum, void (*handler)(int));
static void           drainsignalpipe(void);
static void           sighup(int sig);
static void           sigterm(int sig);
static void           tag(const Arg *arg);
static void           tagmon(const Arg *arg);
static void           tile(Monitor *);
static void           togglefloating(const Arg *arg);
static void           togglefullscr(const Arg *arg);
static void           toggletag(const Arg *arg);
static void           toggleview(const Arg *arg);
static void           unfocus(Client *c, bool setfocus);
static void           unmanage(Client *c, bool destroyed);
static void           unmapnotify(XEvent *e);
static bool           updategeom(void);
static bool           updateworkareas(void);
static void           reflowworkareas(void);
static bool           windowisdock(Window w);
static void           updateclientlist(void);
static void           updatenumlockmask(void);
static void           updatesizehints(Client *c);
static void           updatewindowtype(Client *c);
static void           updateclass(Client *c);
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
static int            initruntimeconfig(FILE *err);
static void           freeruntimeconfig(void);
static unsigned int   runtime_modifiers(unsigned int modifiers);

/*********************************************************************
 * Global variables.
 */

static const char     broken[] = "broken";
static RawmControl    control = {0};
static uint64_t       feedback_serial;
static Atom           dockatom, surfaceatom, utf8atom;
static int            screen;
static int            sw, sh; /* X display screen geometry width, height */
static int           (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int   numlockmask = 0;
static void          (*handler[LASTEvent]) (XEvent *) = {
  [ButtonPress]       = buttonpress,
  [ClientMessage]     = clientmessage,
  [ConfigureRequest]  = configurerequest,
  [ConfigureNotify]   = configurenotify,
  [DestroyNotify]     = destroynotify,
  [EnterNotify]       = enternotify,
  [FocusIn]           = focusin,
  [MappingNotify]     = mappingnotify,
  [MapNotify]         = mapnotify,
  [MapRequest]        = maprequest,
  [MotionNotify]      = motionnotify,
  [PropertyNotify]    = propertynotify,
  [UnmapNotify]       = unmapnotify
};

static Atom           wmatom[WMLast], netatom[NetLast];
static Atom           rawmatom[RawmAtomLast];

static volatile sig_atomic_t restart = 0;
static volatile sig_atomic_t running = 1;
static int            signalpipe[2] = { -1, -1 };
static Cursor         cursor[CurLast];
static Display       *dpy;
static DC             dc;
static Monitor       *mons = NULL, *selmon = NULL;
static Window         root;

static RawmConfig     config;
static Layout         layouts[RAWM_LAYOUT_COUNT] = {
  { tile },
  { NULL },
  { monocle },
  { bstack },
  { bstackhoriz },
  { grid },
};
static Button        *buttons = NULL;
static size_t         button_count = 0;
static unsigned int   opacity = 0xffffffffu;

struct Pertag {
  unsigned int  curtag, prevtag;     /* current and previous tag           */
  int           nmasters[RAWM_MAX_TAGS + 1];  /* number of windows in master area   */
  float         mfacts[RAWM_MAX_TAGS + 1];    /* mfacts per tag                     */
  unsigned int  sellts[RAWM_MAX_TAGS + 1];    /* selected layouts                   */
  const Layout *ltidxs[RAWM_MAX_TAGS + 1][2]; /* matrix of tags and layouts indexes */
};

/*********************************************************************
 * Function implementations.
 */

static unsigned int
runtime_modifiers(unsigned int modifiers)
{
  unsigned int mask = 0;

  if (modifiers & RAWM_MOD_SHIFT)   mask |= ShiftMask;
  if (modifiers & RAWM_MOD_CONTROL) mask |= ControlMask;
  if (modifiers & RAWM_MOD_1)       mask |= Mod1Mask;
  if (modifiers & RAWM_MOD_2)       mask |= Mod2Mask;
  if (modifiers & RAWM_MOD_3)       mask |= Mod3Mask;
  if (modifiers & RAWM_MOD_4)       mask |= Mod4Mask;
  if (modifiers & RAWM_MOD_5)       mask |= Mod5Mask;

  return mask;
}

static void (*
pointer_action_function(RawmPointerAction action))(const Arg *)
{
  switch (action)
  {
    case RAWM_POINTER_MOVE:            return movemouse;
    case RAWM_POINTER_RESIZE:          return resizemouse;
    case RAWM_POINTER_TOGGLE_FLOATING: return togglefloating;
    default:                          return NULL;
  }
}

static int
initruntimeconfig(FILE *err)
{
  size_t i;

  buttons = calloc(config.pointer_binding_count, sizeof(*buttons));
  if (config.pointer_binding_count && !buttons)
  {
    fputs("rawm: out of memory while building pointer configuration\n", err);
    return -1;
  }

  for (i = 0; i < config.pointer_binding_count; ++i)
  {
    void (*func)(const Arg *);

    /* Only admitted-client pointer mechanics remain rawm authority. */
    func = pointer_action_function(config.pointer_bindings[i].action);
    if (!func)
      continue;

    buttons[button_count].mask = runtime_modifiers(config.pointer_bindings[i].modifiers);
    buttons[button_count].button = config.pointer_bindings[i].button;
    buttons[button_count].func = func;
    buttons[button_count].arg = (Arg){0};
    ++button_count;
  }

  opacity = (unsigned int)(config.opacity * 4294967295.0);
  return 0;
}

static void
freeruntimeconfig(void)
{
  free(buttons);
  buttons = NULL;
  button_count = 0;
}

char *
getprop(Window w, const char *prop)
{
  Atom            atom, adummy;
  unsigned char  *val = NULL;
  int             idummy;
  unsigned long   ldummy;

  atom = XInternAtom(dpy, prop, true);

  if (atom)
  {
    XGetWindowProperty(dpy, w, atom, 0, BUFSIZ, false, XA_STRING,
                       &adummy, &idummy, &ldummy, &ldummy, &val);
  }

  return (char *)val;
}

static void
applyrules(Client *c,
           bool *has_fullscreen,
           bool *fullscreen,
           bool *has_attention,
           RawmAttentionMode *attention)
{
  unsigned int   i;
  const char    *class, *instance, *role;
  const RawmRule *r;
  Monitor       *m;
  XClassHint     ch = { NULL, NULL };

  /* rule matching */
  XGetClassHint(dpy, c->win, &ch);

  class     = ch.res_class ? ch.res_class : broken;
  instance  = ch.res_name  ? ch.res_name  : broken;
  snprintf(c->class_name, sizeof(c->class_name), "%s",
           ch.res_class ? ch.res_class : "");
  role      = (role = getprop(c->win, "WM_WINDOW_ROLE"))
            ? role
            : broken;

  for (i = 0; i < config.rule_count; i++)
  {
    r = &config.rules[i];

    if (   (!r->title    || strstr(c->name,  r->title))
        && (!r->class_name    || strstr(class,    r->class_name))
        && (!r->instance || strstr(instance, r->instance))
        && (!r->role     || strstr(role,     r->role))
        )
    {
      c->isfloating  = r->floating;
      c->tags       |= r->tags;

      if (r->has_center) {
        c->center = r->center;
        c->center_explicit = true;
      }

      if (r->has_fullscreen) {
        *fullscreen = r->fullscreen;
        *has_fullscreen = true;
      }

      if (r->has_attention) {
        *attention = r->attention;
        *has_attention = true;
      }

      for (m = mons;  m && m->num != r->monitor;  m = m->next)
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

static void
centerclient(Client *c, RawmCenterMode mode)
{
  Client *parent = NULL;
  Window trans = None;
  int x, y, maxx, maxy;

  if (mode == RAWM_CENTER_NONE)
    return;

  if (mode == RAWM_CENTER_PARENT &&
      XGetTransientForHint(dpy, c->win, &trans) &&
      (parent = wintoclient(trans)) && parent != c && parent->mon == c->mon) {
    x = parent->x + (WIDTH(parent) - WIDTH(c)) / 2;
    y = parent->y + (HEIGHT(parent) - HEIGHT(c)) / 2;
  } else {
    x = c->mon->mx + (c->mon->mw - WIDTH(c)) / 2;
    y = c->mon->my + (c->mon->mh - HEIGHT(c)) / 2;
  }

  maxx = c->mon->mx + c->mon->mw - WIDTH(c);
  maxy = c->mon->my + c->mon->mh - HEIGHT(c);
  if (maxx < c->mon->mx)
    maxx = c->mon->mx;
  if (maxy < c->mon->my)
    maxy = c->mon->my;

  c->x = MAX(c->mon->mx, MIN(x, maxx));
  c->y = MAX(c->mon->my, MIN(y, maxy));
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


  if (   config.size_hints == RAWM_SIZE_HINTS_ALWAYS
      || (config.size_hints == RAWM_SIZE_HINTS_FLOATING && clientisfree(c)))
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
  {
    for (m = mons; m; m = m->next)
      showhide(m->stack);
  }

  if (m)
    arrangemon(m);
  else
  {
    for (m = mons; m; m = m->next)
      arrangemon(m);
  }
}

static void
arrangemon(Monitor *m)
{
  unsigned int n = 0;
  Client *c;
  unsigned int border;

  for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
    n++;

  for (c = m->clients; c; c = c->next)
  {
    if (!ISVISIBLE(c) || c->isfullscreen)
      continue;

    border = config.border_width;
    if (   m->lt[m->sellt]->arrange
        && !c->isfloating
        && (m->lt[m->sellt]->arrange == monocle || n == 1))
      border = 0;

    if (c->bw != (int)border)
    {
      c->oldbw = c->bw;
      c->bw = (int)border;
      XSetWindowBorderWidth(dpy, c->win, border);
      configure(c);
    }

    if (m == selmon && m->sel == c)
      XSetWindowBorder(dpy, c->win,
                       dc.colors[clientselectedscheme(c)][ColBorder]);
  }

  if (m->lt[m->sellt]->arrange)
    m->lt[m->sellt]->arrange(m);

  for (c = m->clients; c; c = c->next)
    updateclientcomposition(c);

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

typedef struct {
  int x;
  int y;
  int w;
  int h;
} LayoutArea;

static int
fitgap(unsigned int configured, int extent, int items)
{
  int maxgap;

  if (items <= 1 || extent <= items)
    return 0;

  maxgap = (extent - items) / (items - 1);
  return MIN((int)configured, maxgap);
}

static LayoutArea
layoutarea(Monitor *m, int n)
{
  LayoutArea area;
  int outer = (config.smart_gaps && n == 1) ? 0 : (int)config.gap_outer;
  int limit = MAX(0, (MIN(m->ww, m->wh) - 1) / 2);

  outer = MIN(outer, limit);
  area.x = m->wx + outer;
  area.y = m->wy + outer;
  area.w = MAX(1, m->ww - 2 * outer);
  area.h = MAX(1, m->wh - 2 * outer);
  return area;
}

static void
bstack(Monitor *m)
{
  LayoutArea area;
  int     w, h, mh, mx, tx, ty, tw;
  int     master, stack, mgap, sgap, splitgap;
  int     i, n;
  Client *c;

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), n++)
    /* NOTHING */;

  if (n == 0)
    return;

  area = layoutarea(m, n);
  master = MIN(n, m->nmaster);
  stack = n - master;
  mgap = fitgap(config.gap_inner, area.w, master);
  sgap = fitgap(config.gap_inner, area.w, stack);
  splitgap = (master && stack) ? fitgap(config.gap_inner, area.h, 2) : 0;

  if (n > m->nmaster)
  {
    mh = m->nmaster ? m->mfact * (area.h - splitgap) : 0;
    tw = stack ? (area.w - sgap * (stack - 1)) / stack : area.w;
    ty = area.y + mh + splitgap;
  }
  else
  {
    mh = area.h;
    tw = area.w;
    ty = area.y;
  }

  for (i = mx = 0, tx = area.x, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), i++)
  {
    if (i < m->nmaster)
    {
      w = (area.w - mx - mgap * (master - i - 1)) / (master - i);
      resize(c,
             area.x + mx,
             area.y,
             w  - (2 * c->bw),
             mh - (2 * c->bw),
             false);
      mx += WIDTH(c) + mgap;
    }
    else
    {
      h = area.h - mh - splitgap;
      resize(c,
             tx,
             ty,
             tw - (2 * c->bw),
             h  - (2 * c->bw),
             false);
      if (tw != area.w)
        tx += WIDTH(c) + sgap;
    }
  }
}

static void
bstackhoriz(Monitor *m)
{
  LayoutArea area;
  int     w, mh, mx, tx, ty, th;
  int     master, stack, mgap, sgap, splitgap, stackh;
  int     i, n;
  Client *c;

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), n++)
    /* NOTHING */;

  if (n == 0)
    return;

  area = layoutarea(m, n);
  master = MIN(n, m->nmaster);
  stack = n - master;
  mgap = fitgap(config.gap_inner, area.w, master);
  splitgap = (master && stack) ? fitgap(config.gap_inner, area.h, 2) : 0;
  stackh = area.h;
  sgap = 0;

  if (n > m->nmaster)
  {
    mh = m->nmaster ? m->mfact * (area.h - splitgap) : 0;
    stackh = area.h - mh - splitgap;
    sgap = fitgap(config.gap_inner, stackh, stack);
    th = stack ? (stackh - sgap * (stack - 1)) / stack : stackh;
    ty = area.y + mh + splitgap;
  }
  else
  {
    th = mh = area.h;
    ty =      area.y;
  }

  for (i = mx = 0, tx = area.x, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), i++)
  {
    if (i < m->nmaster)
    {
      w = (area.w - mx - mgap * (master - i - 1)) / (master - i);
      resize(c,
             area.x + mx,
             area.y,
             w  - (2 * c->bw),
             mh - (2 * c->bw),
             false);
      mx += WIDTH(c) + mgap;
    }
    else
    {
      resize(c,
             tx,
             ty,
             area.w - (2 * c->bw),
             th    - (2 * c->bw),
             false);
      if (th != area.h)
        ty += HEIGHT(c) + sgap;
    }
  }
}

static void
buttonpress(XEvent *e)
{
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;
  size_t i;

  if ((m = wintomon(ev->window)) && m != selmon)
  {
    unfocus(selmon->sel, true);
    selmon = m;
    focus(NULL);
  }

  c = wintoclient(ev->window);
  if (!c)
    return;

  focus(c);
  for (i = 0; i < button_count; ++i)
  {
    if (!buttons[i].func || buttons[i].button != ev->button ||
        CLEANMASK(buttons[i].mask) != CLEANMASK(ev->state))
      continue;
    buttons[i].func(&buttons[i].arg);
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
  Arg a = { .ui = ~0 };
  Layout foo = { NULL };
  Monitor *m;

  view(&a);
  selmon->lt[selmon->sellt] = &foo;

  for (m = mons; m; m = m->next)
    while (m->stack)
      unmanage(m->stack, false);


  while (mons)
    cleanupmon(mons);

  XSync(dpy, false);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  rawm_control_destroy(&control);
  if (signalpipe[0] >= 0)
    close(signalpipe[0]);
  if (signalpipe[1] >= 0)
    close(signalpipe[1]);
  signalpipe[0] = signalpipe[1] = -1;
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


  if (mon->pertag)
    free(mon->pertag);

  free(mon);
}

static bool
clienturgent(const Client *c)
{
  return c->app_urgent || c->wm_urgent;
}

static void
clearurgent(Client *c)
{
  XWMHints *wmh;

  c->app_urgent = false;
  c->wm_urgent  = false;

  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;

  wmh->flags &= ~XUrgencyHint;
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

static uint32_t
clientmessagecard32(const XClientMessageEvent *message, int index)
{
  return (uint32_t)message->data.l[index];
}

static bool
controlargumenttag(int32_t selector, Arg *arg, bool allow_previous)
{
  if (!arg)
    return false;

  if (selector == -1)
    arg->ui = ~0u;
  else if (selector == 0 && allow_previous)
    arg->ui = 0u;
  else if (selector > 0 && (size_t)selector <= config.tag_count)
    arg->ui = 1u << ((unsigned int)selector - 1u);
  else
    return false;

  return true;
}

static int
layoutid(const Layout *layout)
{
  int i;

  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i)
  {
    if (layout == &layouts[i])
      return i;
  }
  return -1;
}

static void
controlwriteescaped(FILE *stream, const char *text)
{
  const unsigned char *p = (const unsigned char *)text;

  for (; p && *p; ++p)
  {
    if (*p == '%' || *p == '\t' || *p == '\n' || *p == '\r')
      fprintf(stream, "%%%02X", *p);
    else
      fputc(*p, stream);
  }
}

static RawmTagState
monitortagstate(const Monitor *m)
{
  RawmTagState state = { 0 };
  const Client *c;

  if (!m)
    return state;

  state.viewed = m->tagset[m->seltags] & TAGMASK;
  for (c = m->clients; c; c = c->next)
  {
    state.occupied |= c->tags & TAGMASK;
    if (clienturgent(c))
      state.urgent |= c->tags & TAGMASK;
  }

  return state;
}

static void
publishfeedback(const char *event, const Monitor *m,
                int32_t arg0, int32_t arg1)
{
  char record[192];
  int length;

  if (!control.display || !event || !*event || !m)
    return;

  feedback_serial++;
  length = snprintf(record, sizeof record,
                    "rawm-feedback-v1\t%llu\t%s\t%d\t%d\t%d\n",
                    (unsigned long long)feedback_serial, event, m->num,
                    arg0, arg1);
  if (length < 0 || (size_t)length >= sizeof record)
  {
    fputs("rawm: transient feedback record is too long\n", stderr);
    return;
  }

  (void)rawm_control_publish_feedback(&control, record, (size_t)length, stderr);
}

static void
publishcontrolstate(void)
{
  char *snapshot = NULL;
  size_t length = 0;
  FILE *stream;
  Monitor *m;

  if (!control.display)
    return;

  stream = open_memstream(&snapshot, &length);
  if (!stream)
  {
    fputs("rawm: could not allocate control-state stream\n", stderr);
    return;
  }

  fprintf(stream, "rawm-state-v1\n");
  fprintf(stream, "tag-count\t%zu\n", config.tag_count);
  fprintf(stream, "selected-monitor\t%d\n", selmon ? selmon->num : -1);

  for (m = mons; m; m = m->next)
  {
    RawmTagState state = monitortagstate(m);
    int id = layoutid(m->lt[m->sellt]);
    const char *layout = id >= 0 ? rawm_layout_name((RawmLayoutId)id) : NULL;
    size_t i;

    fprintf(stream,
            "monitor\t%d\t%d\t%d\t%d\t%d\t%d\t%08x\t%08x\t%08x\t%s\t0x%lx\t%d\t%.6f\n",
            m->num, m == selmon ? 1 : 0,
            m->mx, m->my, m->mw, m->mh,
            state.viewed, state.occupied, state.urgent,
            layout ? layout : "unknown",
            m->sel ? (unsigned long)m->sel->win : 0ul,
            m->nmaster, (double)m->mfact);

    {
      const Client *c;
      unsigned int visible_count = 0;
      unsigned int focused_index = 0;

      for (c = m->clients; c; c = c->next)
      {
        if (!ISVISIBLE(c))
          continue;
        ++visible_count;
        if (c == m->sel)
          focused_index = visible_count;
      }
      fprintf(stream, "stack\t%d\t%u\t%u\n",
              m->num, focused_index, visible_count);
    }

    for (i = 0; i < config.tag_count; ++i)
    {
      fprintf(stream, "tag\t%d\t%zu\t", m->num, i + 1u);
      controlwriteescaped(stream, m->tag_names[i]);
      fputc('\n', stream);
    }
  }

  if (fclose(stream) != 0)
  {
    free(snapshot);
    fputs("rawm: could not finalize control state\n", stderr);
    return;
  }

  (void)rawm_control_publish_state(&control, snapshot, length, stderr);
  free(snapshot);
  publishcontrolclients();
}

static void
publishcontrolclients(void)
{
  char *snapshot = NULL;
  size_t length = 0;
  FILE *stream;
  Monitor *m;

  if (!control.display)
    return;

  stream = open_memstream(&snapshot, &length);
  if (!stream)
  {
    fputs("rawm: could not allocate control-client stream\n", stderr);
    return;
  }

  fputs("rawm-clients-v1\n", stream);
  for (m = mons; m; m = m->next)
  {
    Client *c;

    for (c = m->clients; c; c = c->next)
    {
      fprintf(stream, "client\t0x%lx\t%d\t%08x\t%d\t%d\t",
              (unsigned long)c->win, m->num, c->tags,
              ISVISIBLE(c) ? 1 : 0,
              (m == selmon && c == m->sel) ? 1 : 0);
      controlwriteescaped(stream, c->class_name);
      fputc('\t', stream);
      controlwriteescaped(stream, c->name);
      fputc('\n', stream);
    }
  }

  if (fclose(stream) != 0)
  {
    free(snapshot);
    fputs("rawm: could not finalize control-client state\n", stderr);
    return;
  }

  (void)rawm_control_publish_clients(&control, snapshot, length, stderr);
  free(snapshot);
}

static void
activateclient(Client *c)
{
  if (!c)
    return;

  if (!ISVISIBLE(c))
  {
    c->mon->seltags ^= 1;
    c->mon->tagset[c->mon->seltags] = c->tags;
  }
  pop(c);
}

static void
handlecontrolcommand(const RawmControlRequest *request)
{
  Arg arg = {0};

  if (!request || !selmon)
    return;

  switch (request->command)
  {
    case RAWM_CONTROL_FOCUS_STACK:
      if (request->arg0 == -1 || request->arg0 == 1)
      {
        arg.i = request->arg0;
        focusstack(&arg);
      }
      break;
    case RAWM_CONTROL_FOCUS_INDEX:
      if (request->arg0 > 0)
      {
        arg.i = request->arg0;
        focusnstack(&arg);
      }
      break;
    case RAWM_CONTROL_FOCUS_CLIENT:
      activateclient(wintoclient((Window)(uint32_t)request->arg0));
      break;
    case RAWM_CONTROL_INC_MASTER:
      if (request->arg0 != 0)
      {
        arg.i = request->arg0;
        incnmaster(&arg);
      }
      break;
    case RAWM_CONTROL_SET_MASTER_FACTOR:
      if (request->arg1 == 0)
      {
        arg.f = (float)request->arg0 / (float)RAWM_CONTROL_FACTOR_SCALE;
        setmfact(&arg);
      }
      else if (request->arg1 == 1 &&
               request->arg0 >= RAWM_CONTROL_FACTOR_SCALE / 10 &&
               request->arg0 <= RAWM_CONTROL_FACTOR_SCALE * 9 / 10)
      {
        arg.f = 1.0f + (float)request->arg0 /
                         (float)RAWM_CONTROL_FACTOR_SCALE;
        setmfact(&arg);
      }
      break;
    case RAWM_CONTROL_ZOOM:
      zoom(NULL);
      break;
    case RAWM_CONTROL_VIEW:
      if (controlargumenttag(request->arg0, &arg, true))
        view(&arg);
      break;
    case RAWM_CONTROL_TOGGLE_VIEW:
      if (controlargumenttag(request->arg0, &arg, false))
        toggleview(&arg);
      break;
    case RAWM_CONTROL_CYCLE_VIEW:
      if (request->arg0 == -1 || request->arg0 == 1)
      {
        arg.i = request->arg0;
        cycleview(&arg);
      }
      break;
    case RAWM_CONTROL_KILL_CLIENT:
      killclient(NULL);
      break;
    case RAWM_CONTROL_SET_LAYOUT:
      if (request->arg0 == -1)
        setlayout(NULL);
      else if (request->arg0 >= 0 && request->arg0 < RAWM_LAYOUT_COUNT)
      {
        arg.v = &layouts[request->arg0];
        setlayout(&arg);
      }
      break;
    case RAWM_CONTROL_CYCLE_LAYOUT:
      if (request->arg0 == -1 || request->arg0 == 1)
      {
        arg.i = request->arg0;
        cyclelayout(&arg);
      }
      break;
    case RAWM_CONTROL_TOGGLE_FLOATING:
      togglefloating(&arg);
      break;
    case RAWM_CONTROL_TAG:
      if (controlargumenttag(request->arg0, &arg, false))
        tag(&arg);
      break;
    case RAWM_CONTROL_TOGGLE_TAG:
      if (controlargumenttag(request->arg0, &arg, false))
        toggletag(&arg);
      break;
    case RAWM_CONTROL_WINDOW_VIEW:
      winview(NULL);
      break;
    case RAWM_CONTROL_TOGGLE_FULLSCREEN:
      togglefullscr(NULL);
      break;
    case RAWM_CONTROL_FOCUS_MONITOR:
      if (request->arg0 == -1 || request->arg0 == 1)
      {
        arg.i = request->arg0;
        focusmon(&arg);
      }
      break;
    case RAWM_CONTROL_TAG_MONITOR:
      if (request->arg0 == -1 || request->arg0 == 1)
      {
        arg.i = request->arg0;
        tagmon(&arg);
      }
      break;
    case RAWM_CONTROL_RENAME_TAG:
    {
      char name[RAWM_TAG_NAME_MAX + 1u];
      if (rawm_control_read_text(&control, request->requestor,
                                 name, sizeof(name), stderr) >= 0)
        settagname(name);
      break;
    }
    case RAWM_CONTROL_QUIT:
      if (request->arg0 == 0 || request->arg0 == 1)
      {
        arg.i = request->arg0;
        quit(&arg);
      }
      break;
    case RAWM_CONTROL_COMMAND_LAST:
      break;
  }
}

static void
clientmessage(XEvent *e)
{
  XClientMessageEvent *cme = &e->xclient;
  RawmControlRequest request;
  RawmControlMessageResult control_message;
  Client *c;

  control_message = rawm_control_decode(&control, cme, &request);
  if (control_message != RAWM_CONTROL_MESSAGE_OTHER)
  {
    if (control_message == RAWM_CONTROL_MESSAGE_COMMAND)
      handlecontrolcommand(&request);
    return;
  }

  c = wintoclient(cme->window);

  if (!c)
    return;

  if (cme->message_type == netatom[NetWMState])
  {
    if (   clientmessagecard32(cme, 1) == netatom[NetWMFullscreen]
        || clientmessagecard32(cme, 2) == netatom[NetWMFullscreen]
        )
    {
      setfullscreen(c,
                    (       clientmessagecard32(cme, 0) == 1 /* _NET_WM_STATE_ADD    */
                     || (   clientmessagecard32(cme, 0) == 2 /* _NET_WM_STATE_TOGGLE */
                         && !c->isfullscreen  )
                     )
                    );
    }
  }
  else if (cme->message_type == netatom[NetActiveWindow])
    activateclient(c);
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
  XConfigureEvent *ev = &e->xconfigure;

  if (ev->window != root)
    return;

  {
    bool dirty = (sw != ev->width || sh != ev->height);
    sw = ev->width;
    sh = ev->height;
    if (updategeom() || dirty)
    {
      (void)updateworkareas();
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
    else if (c->isfloating || !c->mon->lt[c->mon->sellt]->arrange)
    {
      int x = c->x;
      int y = c->y;
      int w = c->w;
      int h = c->h;

      m = c->mon;
      if (ev->value_mask & CWX)
        x = ev->x;
      if (ev->value_mask & CWY)
        y = ev->y;
      if (ev->value_mask & CWWidth)
        w = ev->width;
      if (ev->value_mask & CWHeight)
        h = ev->height;

      if ((x + w + 2 * c->bw) > m->mx + m->mw && c->isfloating)
        x = m->mx + (m->mw / 2 - (w + 2 * c->bw) / 2);
      if ((y + h + 2 * c->bw) > m->my + m->mh && c->isfloating)
        y = m->my + (m->mh / 2 - (h + 2 * c->bw) / 2);

      resize(c, x, y, w, h, false);
      if (    (ev->value_mask & (CWX | CWY))
          && !(ev->value_mask & (CWWidth | CWHeight)))
        configure(c);
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
createmon(int idx)
{
  Monitor *m;
  size_t i;
  const RawmTagConfig *tagcfg;

  if (!(m = (Monitor *)calloc(1, sizeof(Monitor))))
    die("fatal: could not malloc() %u bytes\n", sizeof(Monitor));

  tagcfg = rawm_config_tags_for_monitor(&config, idx);
  m->num        = idx;
  m->tagset[0]  = m->tagset[1] = 1;
  m->mfact      = (float)config.master_factor;
  m->nmaster    = config.master_count;
  m->lt[0]      = &layouts[tagcfg[0].layout];
  m->lt[1]      = &layouts[RAWM_LAYOUT_FLOATING];

  for (i = 0; i < config.tag_count; ++i)
    snprintf(m->tag_names[i], sizeof m->tag_names[i], "%s", tagcfg[i].name);


  if (!(m->pertag = (Pertag *)calloc(1, sizeof(Pertag))))
    die("fatal: could not malloc() %u bytes\n", sizeof(Pertag));

  m->pertag->curtag = m->pertag->prevtag = 1;

  for (i = 0; i <= config.tag_count; i++)
  {
    m->pertag->nmasters[i] = m->nmaster;
    m->pertag->mfacts[i]   = m->mfact;
    m->pertag->ltidxs[i][0] = i == 0
      ? &layouts[tagcfg[0].layout]
      : &layouts[tagcfg[i - 1].layout];
    m->pertag->ltidxs[i][1] = &layouts[RAWM_LAYOUT_FLOATING];
    m->pertag->sellts[i]    = m->sellt;
  }

  return m;
}

static void
cyclelayout(const Arg *arg)
{
  const Layout *current;
  Arg next = {0};
  int index = -1;
  int direction;

  if (!arg || !arg->i)
    return;

  current = selmon->lt[selmon->sellt];
  for (int i = 0; i < RAWM_LAYOUT_COUNT; i++)
  {
    if (current == &layouts[i])
    {
      index = i;
      break;
    }
  }

  if (index < 0)
    return;

  direction = arg->i > 0 ? 1 : -1;
  index = (index + direction + RAWM_LAYOUT_COUNT) % RAWM_LAYOUT_COUNT;
  next.v = &layouts[index];
  setlayout(&next);
}

static void
cycleview(const Arg *arg)
{
  Arg next = {0};
  int index;
  int direction;

  if (!arg || !arg->i || !config.tag_count)
    return;

  direction = arg->i > 0 ? 1 : -1;

  if (selmon->pertag->curtag == 0)
    index = direction > 0 ? 0 : (int)config.tag_count - 1;
  else
    index = ((int)selmon->pertag->curtag - 1 + direction +
             (int)config.tag_count) % (int)config.tag_count;

  next.ui = 1u << index;
  view(&next);
}

static void
destroynotify(XEvent *e)
{
  Client              *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;
  bool handled = false;

  if ((c = wintoclient(ev->window)))
  {
    unmanage(c, true);
    handled = true;
  }

  if (!handled)
    reflowworkareas();
}

static void
detach(Client *c)
{
  Client **tc;

  for (tc = &c->mon->clients;
      *tc  &&  (*tc != c);
       tc = &(*tc)->next
       )
    /* NOTHING */;

  *tc = c->next;
}

static void
detachstack(Client *c)
{
  Client **tc, *t;

  for (tc = &c->mon->stack;
      *tc  &&  (*tc != c);
       tc = &(*tc)->snext
       )
    /* NOTHING */;

  *tc = c->snext;

  if (c == c->mon->sel)
  {
    for (t = c->mon->stack;
         t  &&  !ISVISIBLE(t);
         t = t->snext
         )
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
enternotify(XEvent *e)
{
  Client          *c;
  Monitor         *m;
  XCrossingEvent  *ev = &e->xcrossing;

  if (   (   ev->mode   != NotifyNormal
          || ev->detail == NotifyInferior )
      && (   ev->window != root )
      )
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
focus(Client *c)
{
  if (!c  ||  !ISVISIBLE(c))
  {
    for (c = selmon->stack;  c && !ISVISIBLE(c);  c = c->snext)
      /* NOTHING */;
  }

  /* was if (selmon->sel) */
  if (    selmon->sel
      && (selmon->sel != c)
      )
    unfocus(selmon->sel, false);

  if (c)
  {
    if (c->mon != selmon)
      selmon = c->mon;

    if (clienturgent(c))
      clearurgent(c);


    detachstack(c);
    attachstack(c);
    grabbuttons(c, true);
    XSetWindowBorder(dpy, c->win,
                     dc.colors[clientselectedscheme(c)][ColBorder]);
    setfocus(c);
  }
  else
  {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }

  selmon->sel = c;
  if (c)
    updateclientcomposition(c);
}

static void
focusin(XEvent *e)
{
  /* there are some broken focus acquiring clients */
  XFocusChangeEvent *ev = &e->xfocus;

  if (selmon->sel  &&  (ev->window != selmon->sel->win))
    setfocus(selmon->sel);
}

static void
focusmon(const Arg *arg)
{
  Monitor *m;

  if (!mons->next)
    return;

  if ((m = dirtomon(arg->i))  ==  selmon)
    return;

  unfocus(selmon->sel, true);
  selmon = m;
  focus(NULL);
  publishcontrolstate();
  publishfeedback("monitor", selmon, selmon->num, 0);
}

static void
focusnstack(const Arg *arg)
{
  Client *c;
  int visible_index = 0;

  if (!arg || arg->i <= 0)
    return;

  for (c = selmon->clients; c; c = c->next)
  {
    if (!ISVISIBLE(c))
      continue;

    visible_index++;
    if (visible_index != arg->i)
      continue;

    focus(c);
    restack(selmon);
    return;
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
grid(Monitor *m)
{
  LayoutArea area;
  int     n, cols, rows, cn, rn, i, cx, cy, cw, ch;
  int     colgap, rowgap;
  Client *c;

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next))
    n++;

  if (n == 0)
    return;

  area = layoutarea(m, n);

  /* grid dimensions */
  for (cols = 0;  cols <= n/2;  cols++)
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
  colgap = fitgap(config.gap_inner, area.w, cols);

  /* window geometries */
  cw = cols ? ((area.w - colgap * (cols - 1)) / cols) : area.w;
  cn = 0; /* current column number */
  rn = 0; /* current row number    */
  for (i = 0, c = nexttiled(m->clients);
       c;
       i++, c = nexttiled(c->next))
  {
    if (((i / rows) + 1) > (cols - (n % cols)))
      rows = (n / cols) + 1;

    rowgap = fitgap(config.gap_inner, area.h, rows);
    ch = rows ? ((area.h - rowgap * (rows - 1)) / rows) : area.h;
    cx = area.x + cn * (cw + colgap);
    cy = area.y + rn * (ch + rowgap);
    resize(c,
           cx,
           cy,
           cw - 2 * c->bw,
           ch - 2 * c->bw,
           false
           );
    rn++;

    if (rn >= rows)
    {
      rn = 0;
      cn++;
    }
  }
}

static bool
atomlistcontains(Window w, Atom prop, Atom atom)
{
  Atom actual = None;
  int format = 0;
  unsigned long i, nitems = 0, bytes_after = 0;
  unsigned char *data = NULL;
  bool found = false;

  if (XGetWindowProperty(dpy, w, prop, 0L, 1024L, False, XA_ATOM,
                         &actual, &format, &nitems, &bytes_after, &data)
      != Success)
    return false;

  if (actual == XA_ATOM && format == 32 && data) {
    Atom *atoms = (Atom *)data;
    for (i = 0; i < nitems; ++i) {
      if (atoms[i] == atom) {
        found = true;
        break;
      }
    }
  }

  if (data)
    XFree(data);
  return found;
}

static bool
windowisdock(Window w)
{
  return atomlistcontains(w, netatom[NetWMWindowType], dockatom);
}

static bool
getdockstrut(Window w, unsigned long strut[12])
{
  Atom actual = None;
  int format = 0;
  unsigned long nitems = 0, bytes_after = 0;
  unsigned char *data = NULL;
  bool found = false;

  memset(strut, 0, sizeof(unsigned long) * 12u);

  if (XGetWindowProperty(dpy, w, netatom[NetWMStrutPartial], 0L, 12L, False,
                         XA_CARDINAL, &actual, &format, &nitems, &bytes_after,
                         &data) == Success &&
      actual == XA_CARDINAL && format == 32 && nitems >= 12u && data)
  {
    memcpy(strut, data, sizeof(unsigned long) * 12u);
    found = true;
  }
  if (data)
    XFree(data);
  if (found)
    return true;

  actual = None;
  format = 0;
  nitems = bytes_after = 0;
  data = NULL;
  if (XGetWindowProperty(dpy, w, netatom[NetWMStrut], 0L, 4L, False,
                         XA_CARDINAL, &actual, &format, &nitems, &bytes_after,
                         &data) == Success &&
      actual == XA_CARDINAL && format == 32 && nitems >= 4u && data)
  {
    memcpy(strut, data, sizeof(unsigned long) * 4u);
    strut[4] = 0u;
    strut[5] = sh > 0 ? (unsigned long)(sh - 1) : 0u;
    strut[6] = 0u;
    strut[7] = sh > 0 ? (unsigned long)(sh - 1) : 0u;
    strut[8] = 0u;
    strut[9] = sw > 0 ? (unsigned long)(sw - 1) : 0u;
    strut[10] = 0u;
    strut[11] = sw > 0 ? (unsigned long)(sw - 1) : 0u;
    found = true;
  }
  if (data)
    XFree(data);
  return found;
}

static void
setatomlistmember(Window w, Atom prop, Atom atom, bool present)
{
  Atom actual = None;
  Atom *next = NULL;
  int format = 0;
  unsigned long i, nitems = 0, bytes_after = 0, count = 0;
  unsigned char *data = NULL;
  bool found = false;

  if (XGetWindowProperty(dpy, w, prop, 0L, 1024L, False, XA_ATOM,
                         &actual, &format, &nitems, &bytes_after, &data)
      != Success)
    return;

  if (actual != None && (actual != XA_ATOM || format != 32)) {
    if (data)
      XFree(data);
    data = NULL;
    nitems = 0;
  }

  next = calloc(nitems + (present ? 1u : 0u), sizeof(*next));
  if (!next && (nitems || present)) {
    if (data)
      XFree(data);
    die("fatal: could not allocate _NET_WM_STATE atom list\n");
  }

  if (data) {
    Atom *atoms = (Atom *)data;
    for (i = 0; i < nitems; ++i) {
      if (atoms[i] == atom) {
        found = true;
        if (!present)
          continue;
      }
      next[count++] = atoms[i];
    }
  }

  if (present && !found)
    next[count++] = atom;

  XChangeProperty(dpy, w, prop, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)next, (int)count);

  free(next);
  if (data)
    XFree(data);
}


static unsigned long
getcolor(const char *colstr)
{
  Colormap cmap = DefaultColormap(dpy, screen);
  XColor color, exact;

  if (!XAllocNamedColor(dpy, cmap, colstr, &color, &exact))
    die("error, cannot allocate color '%s'\n", colstr);

  return color.pixel;
}

static Atom
getatomprop(Client *c, Atom prop)
{
  Atom actual = None;
  Atom atom = None;
  int format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  unsigned char *data = NULL;

  if (XGetWindowProperty(dpy, c->win, prop, 0L, 1L, False, XA_ATOM,
                         &actual, &format, &nitems, &bytes_after, &data)
      == Success &&
      actual == XA_ATOM && format == 32 && nitems >= 1 && data)
    atom = *(Atom *)data;

  if (data)
    XFree(data);

  return atom;
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
                         (unsigned char **)&p
                         )
      !=  Success
      )
    return -1;

  if (n != 0)
    result = *p;

  XFree(p);

  return result;
}


static const char *
clientlayoutname(const Client *c)
{
  static const char *const names[RAWM_LAYOUT_COUNT] = {
    "tile", "floating", "monocle", "bstack", "bstack_horizontal", "grid"
  };
  size_t i;

  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i)
    if (c->mon->lt[c->mon->sellt] == &layouts[i])
      return names[i];

  return "unknown";
}

static void
setcardinalproperty(Window window, Atom atom, unsigned long value)
{
  XChangeProperty(dpy, window, atom, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&value, 1);
}


static bool
clientiselevated(const Client *c)
{
  return c && c->mon &&
         (c->isfloating || !c->mon->lt[c->mon->sellt]->arrange);
}

static int
clientselectedscheme(const Client *c)
{
  return clientiselevated(c) ? SchemeFloating : SchemeSel;
}

static void
updateclientcomposition(Client *c)
{
  const char *layout;
  unsigned long floating;
  unsigned long selected;

  if (!c || !c->mon)
    return;

  floating = clientiselevated(c);
  selected = c->mon == selmon && c->mon->sel == c;
  layout = clientlayoutname(c);

  setcardinalproperty(c->win, rawmatom[RawmAtomFloating], floating);
  setcardinalproperty(c->win, rawmatom[RawmAtomSelected], selected);
  setcardinalproperty(c->win, rawmatom[RawmAtomMonitor],
                      (unsigned long)c->mon->num);
  XChangeProperty(dpy, c->win, rawmatom[RawmAtomLayout], utf8atom, 8,
                  PropModeReplace, (const unsigned char *)layout,
                  (int)strlen(layout));
}

static void
clearclientcomposition(Client *c)
{
  size_t i;

  if (!c)
    return;

  for (i = 0; i < RawmAtomLast; ++i)
    XDeleteProperty(dpy, c->win, rawmatom[i]);
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
      for (unsigned int i = 0; i < button_count; i++)
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
                      None
                      );
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
                  None
                  );
  }
}

static void
incnmaster(const Arg *arg)
{
  selmon->nmaster =
    selmon->pertag->nmasters[selmon->pertag->curtag] =
      MAX(selmon->nmaster + arg->i, 0);

  arrange(selmon);
  publishfeedback("master-count", selmon, selmon->nmaster, 0);
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
  bool            has_rule_fullscreen = false;
  bool            initial_fullscreen = false;
  bool            has_rule_attention = false;
  RawmAttentionMode initial_attention = RAWM_ATTENTION_NONE;

  if (!(c = calloc(1, sizeof(Client))))
    die("fatal: could not malloc() %u bytes\n", sizeof(Client));

  c->win = w;
  updatetitle(c);

  if (config.opacity >= 0  &&  config.opacity <= 1)
  {
    XChangeProperty(dpy,
                    c->win,
                    XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", false),
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (unsigned char *) &opacity,
                    1L
                    );
  }

  if (XGetTransientForHint(dpy, w, &trans))
  {
    c->istransient = true;
    if ((t = wintoclient(trans)))
    {
      c->mon  = t->mon;
      c->tags = t->tags;
    }
    else
      c->mon = selmon;
  }
  else
    c->mon = selmon;

  applyrules(c, &has_rule_fullscreen, &initial_fullscreen,
             &has_rule_attention, &initial_attention);
  if (!has_rule_fullscreen)
    initial_fullscreen = atomlistcontains(c->win, netatom[NetWMState],
                                          netatom[NetWMFullscreen]);

  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldbw = wa->border_width;

  if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
    c->x = c->mon->wx + c->mon->ww - WIDTH(c);
  if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
    c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
  c->x = MAX(c->x, c->mon->wx);
  c->y = MAX(c->y, c->mon->wy);

  c->bw = config.border_width;

  wc.border_width = c->bw;

  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, dc.colors[0][ColBorder]);

  configure(c); /* propagates border_width, if size doesn't change */
  updatewindowtype(c);
  updatesizehints(c);
  updatewmhints(c);

  if (!c->isfloating)
    c->isfloating = c->istransient || c->isfixed;

  if (c->center != RAWM_CENTER_NONE)
    centerclient(c, c->center);
  else
    placefreeclient(c);

  XSelectInput(dpy,
               w,
                 EnterWindowMask
               | FocusChangeMask
               | PropertyChangeMask
               | StructureNotifyMask
               );
  grabbuttons(c, false);

  savefreegeometry(c);

  if (has_rule_fullscreen && !initial_fullscreen)
    setatomlistmember(c->win, netatom[NetWMState], netatom[NetWMFullscreen],
                      false);

  if (initial_fullscreen)
    setfullscreen(c, true);

  if (   has_rule_attention
      && initial_attention == RAWM_ATTENTION_URGENT
      && !ISVISIBLE(c))
    c->wm_urgent = true;

  if (c->isfloating)
    XRaiseWindow(dpy, c->win);

  attach(c);
  attachstack(c);

  /* some windows require this */
  XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h);

  setclientstate(c, NormalState);

  XChangeProperty(dpy,
                  root,
                  netatom[NetClientList],
                  XA_WINDOW,
                  32,
                  PropModeAppend,
                  (unsigned char *) &(c->win),
                  1
                  );

  if (c->mon == selmon)
    unfocus(selmon->sel, false);

  c->mon->sel = c;

  arrange(c->mon);
  XMapWindow(dpy, c->win);
  focus(NULL);
}

static void
mappingnotify(XEvent *e)
{
  XRefreshKeyboardMapping(&e->xmapping);
}

static void
mapnotify(XEvent *e)
{
  XMapEvent *ev = &e->xmap;
  unsigned long strut[12];

  if (!windowisdock(ev->window) || !getdockstrut(ev->window, strut))
    return;

  XSelectInput(dpy, ev->window, PropertyChangeMask);
  reflowworkareas();
}

static void
maprequest(XEvent *e)
{
  static XWindowAttributes  wa;
  XMapRequestEvent         *ev = &e->xmaprequest;


  if (!XGetWindowAttributes(dpy, ev->window, &wa))
    return;

  if (windowisdock(ev->window))
  {
    XSelectInput(dpy, ev->window, PropertyChangeMask);
    XMapWindow(dpy, ev->window);
    return;
  }

  if (wa.override_redirect)
    return;

  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

static void
monocle(Monitor *m)
{
  LayoutArea area;
  int n = 0;
  Client *c;

  for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
    n++;
  if (n == 0)
    return;

  area = layoutarea(m, n);
  for (c = nexttiled(m->clients);  c;  c = nexttiled(c->next))
  {
    resize(c,
           area.x,
           area.y,
           area.w - 2 * c->bw,
           area.h - 2 * c->bw,
           false
           );

  }
}

static void
motionnotify(XEvent *e)
{
  static Monitor *mon = NULL;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window != root)
    return;

  if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon)
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
  bool     moved = false;
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
                   CurrentTime
                   )
      != GrabSuccess)
    return;

  if (!getrootptr(&x, &y))
    return;

  do
  {
    XMaskEvent(dpy,
                 MOUSEMASK
               | ExposureMask
               | SubstructureRedirectMask,
               &ev
               );

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
          && ny <= selmon->wy + selmon->wh
          )
      {
        if (abs(selmon->wx - nx) < config.snap)
          nx = selmon->wx;

        else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < config.snap)
          nx = selmon->wx + selmon->ww - WIDTH(c);


        if (abs(selmon->wy - ny) < config.snap)
          ny = selmon->wy;

        else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < config.snap)
          ny = selmon->wy + selmon->wh - HEIGHT(c);


        if (   !c->isfloating
            &&  selmon->lt[ selmon->sellt ]->arrange
            && (   abs(nx - c->x) > config.snap
                || abs(ny - c->y) > config.snap )
            )
          togglefloating(NULL);
      }

      if (!selmon->lt[ selmon->sellt ]->arrange || c->isfloating)
      {
        int oldx = c->x;
        int oldy = c->y;

        resize(c, nx, ny, c->w, c->h, true);
        if (c->x != oldx || c->y != oldy)
        {
          moved = true;
          publishfeedback("move", c->mon, c->x - c->mon->wx,
                          c->y - c->mon->wy);
        }
      }

      break;
    }
  } while (ev.type != ButtonRelease);

  XUngrabPointer(dpy, CurrentTime);

  if ((m = recttomon(c->x, c->y, c->w, c->h))  !=  selmon)
  {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }

  if (moved)
    publishfeedback("move", c->mon, c->x - c->mon->wx,
                    c->y - c->mon->wy);
}

static void
settagname(const char *name)
{
  size_t i;

  if (!selmon || !name)
    return;

  for (i = 0; i < config.tag_count; i++)
  {
    if (!(selmon->tagset[selmon->seltags] & (1u << i)))
      continue;

    if (name[0])
    {
      snprintf(selmon->tag_names[i], sizeof selmon->tag_names[i],
               "%zu/", i + 1);
      strncat(selmon->tag_names[i], name,
              sizeof selmon->tag_names[i] -
              strlen(selmon->tag_names[i]) - 1);
    }
    else
      snprintf(selmon->tag_names[i], sizeof selmon->tag_names[i],
               "%zu", i + 1);
  }

  publishcontrolstate();
  publishfeedback("view", selmon,
                  (int32_t)(selmon->tagset[selmon->seltags] & TAGMASK), 0);
}

static Client *
nexttiled(Client *c)
{
  for ( ;  c && (c->isfloating || !ISVISIBLE(c));  c = c->next)
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


  if (ev->atom == netatom[NetWMWindowType] ||
      ev->atom == netatom[NetWMStrut] ||
      ev->atom == netatom[NetWMStrutPartial])
    reflowworkareas();

  if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = wintoclient(ev->window)))
  {
    switch (ev->atom)
    {
    case XA_WM_TRANSIENT_FOR:
      if (   !c->isfloating
          &&  XGetTransientForHint(dpy, c->win, &trans)
          && (c->isfloating = (wintoclient(trans))  !=  NULL)
          )
        arrange(c->mon);
      if (c->center == RAWM_CENTER_PARENT) {
        centerclient(c, c->center);
        XMoveWindow(dpy, c->win, clientdisplayx(c), c->y);
        configure(c);
      }
      break;
    case XA_WM_NORMAL_HINTS:
      updatesizehints(c);
      break;
    case XA_WM_HINTS:
      updatewmhints(c);
          if (clienturgent(c))
      {
        /* Set urgent/warning border color (from config.colors[2]) */
        XSetWindowBorder(dpy, c->win, dc.colors[SchemeUrg][ColBorder]);
      }
      break;
    default:
      break;
    }

    if (ev->atom == XA_WM_CLASS)
      updateclass(c);

    if (   ev->atom == XA_WM_NAME
        || ev->atom == netatom[NetWMName]
        )
    {
      updatetitle(c);
    }

    if (ev->atom == netatom[NetWMWindowType]) {
      updatewindowtype(c);
      if (c->center != RAWM_CENTER_NONE) {
        centerclient(c, c->center);
        XMoveWindow(dpy, c->win, clientdisplayx(c), c->y);
        configure(c);
      }
    }
  }
}

static void
quit(const Arg *arg)
{
  if (arg->i)
    restart = true;

  running = false;
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

    if ((a = INTERSECT(x, y, w, h, m))  >  area)
    {
      area = a;
      r = m;
    }
  }

  return r;
}


static bool
clientisfree(const Client *c)
{
  return c->isfloating || !c->mon->lt[c->mon->sellt]->arrange;
}

static int
clientdisplayx(const Client *c)
{
  return ISVISIBLE(c) ? c->x : WIDTH(c) * -2;
}

static void
placefreeclient(Client *c)
{
  Client *reference;
  Monitor *m;
  int maxx, maxy, startx, starty, x, y;
  int step;

  if (!c || !clientisfree(c) || c->isfullscreen ||
      config.floating_placement != RAWM_FLOATING_PLACEMENT_CASCADE ||
      c->position_authority == RAWM_POSITION_USER ||
      c->isdialog || c->istransient)
    return;

  m = c->mon;
  step = (int)config.cascade_step;
  maxx = m->wx + m->ww - WIDTH(c);
  maxy = m->wy + m->wh - HEIGHT(c);
  if (maxx < m->wx)
    maxx = m->wx;
  if (maxy < m->wy)
    maxy = m->wy;

  startx = MIN(m->wx + step, maxx);
  starty = MIN(m->wy + step, maxy);
  x = startx;
  y = starty;

  reference = m->sel;
  if (reference && reference != c && reference->mon == m &&
      ISVISIBLE(reference) && clientisfree(reference) &&
      !reference->isfullscreen)
  {
    x = reference->x + step;
    y = reference->y + step;
    if (x > maxx || y > maxy)
    {
      x = startx;
      y = starty;
    }
    else
    {
      x = MAX(m->wx, x);
      y = MAX(m->wy, y);
    }
  }

  c->x = x;
  c->y = y;
}

static void
savefreegeometry(Client *c)
{
  if (!c || c->isfullscreen)
    return;

  c->freegeom.x = c->x;
  c->freegeom.y = c->y;
  c->freegeom.w = c->w;
  c->freegeom.h = c->h;
  c->freegeom_valid = true;
}

static void
restorefreegeometry(Client *c)
{
  if (!c || c->isfullscreen || !c->freegeom_valid)
    return;

  resize(c,
         c->freegeom.x,
         c->freegeom.y,
         c->freegeom.w,
         c->freegeom.h,
         false);
}

static void
resize(Client *c, int x, int y, int w, int h, bool interact)
{
  if (applysizehints(c, &x, &y, &w, &h, interact))
  {
    resizeclient(c, x, y, w, h);
    if (clientisfree(c) && !c->isfullscreen)
      savefreegeometry(c);
  }
}

static void
resizeclient(Client *c, int x, int y, int w, int h)
{
  XWindowChanges wc;

  c->oldx = c->x;  c->x = x;
  c->oldy = c->y;  c->y = y;
  c->oldw = c->w;  c->w = wc.width  = w;
  c->oldh = c->h;  c->h = wc.height = h;
  wc.x = clientdisplayx(c);
  wc.y = c->y;
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
  bool     resized = false;
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
                   CurrentTime
                   )
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
               c->h + c->bw - 1
               );

  do
  {
    XMaskEvent(dpy,
                 MOUSEMASK
               | ExposureMask
               | SubstructureRedirectMask,
               &ev
               );

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

      if (   c->mon->wx + nw  >=  selmon->wx
          && c->mon->wx + nw  <=  selmon->wx + selmon->ww
          && c->mon->wy + nh  >=  selmon->wy
          && c->mon->wy + nh  <=  selmon->wy + selmon->wh)
      {
        if (   !c->isfloating
            &&  selmon->lt[selmon->sellt]->arrange
            && (   abs(nw - c->w) > config.snap
                || abs(nh - c->h) > config.snap )
            )
          togglefloating(NULL);
      }

      if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
      {
        int oldw = c->w;
        int oldh = c->h;

        resize(c, c->x, c->y, nw, nh, true);
        if (c->w != oldw || c->h != oldh)
        {
          resized = true;
          publishfeedback("resize", c->mon, c->w, c->h);
        }
      }

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
               c->h + c->bw - 1
               );
  XUngrabPointer(dpy, CurrentTime);

  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    /* NOTHING */;

  if ((m = recttomon(c->x, c->y, c->w, c->h))  !=  selmon)
  {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }

  if (resized)
    publishfeedback("resize", c->mon, c->w, c->h);
}


static void
restack(Monitor *m)
{
  Client *c;
  XEvent ev;

  if (!m->sel)
    return;

  if (m->lt[m->sellt]->arrange)
    for (c = m->stack; c; c = c->snext)
      if (!c->isfloating && ISVISIBLE(c))
        XLowerWindow(dpy, c->win);

  if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
    XRaiseWindow(dpy, m->sel->win);

  XSync(dpy, false);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    /* NOTHING */;
}

static void
run(void)
{
  XEvent ev;
  struct pollfd fds[2];

  XSync(dpy, false);
  while (running)
  {
    int poll_result;

    while (running && XPending(dpy))
    {
      XNextEvent(dpy, &ev);
      if (handler[ev.type])
        handler[ev.type](&ev);
    }
    if (!running)
      break;

    publishcontrolstate();

    memset(fds, 0, sizeof(fds));
    fds[0].fd = ConnectionNumber(dpy);
    fds[0].events = POLLIN;
    fds[1].fd = signalpipe[0];
    fds[1].events = POLLIN;

    poll_result = poll(fds, 2, -1);
    if (poll_result < 0)
    {
      if (errno == EINTR)
        continue;
      die("fatal: poll failed: %s\n", strerror(errno));
    }
    if (poll_result == 0)
    {
      continue;
    }

    if (fds[1].revents & POLLIN)
      drainsignalpipe();
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
      if (  !XGetWindowAttributes(dpy, wins[i], &wa)
          || wa.override_redirect
          || windowisdock(wins[i])
          || XGetTransientForHint(dpy, wins[i], &d1)
          )
        continue;

      if (   wa.map_state      == IsViewable
          || getstate(wins[i]) == IconicState
          )
        manage(wins[i], &wa);
    }

    for (unsigned int i = 0; i < num; i++)
    {
      /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
        continue;

      if (   !windowisdock(wins[i])
          && XGetTransientForHint(dpy, wins[i], &d1)
          && (   wa.map_state      == IsViewable
              || getstate(wins[i]) == IconicState )
          )
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
                  (unsigned char *) data,
                  2
                  );
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
    while (!exists && n--)
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
                    1
                    );
  }

  sendevent(c, wmatom[WMTakeFocus]);
}

static void
setfullscreen(Client *c, bool fullscreen)
{
  if (fullscreen)
  {
    setatomlistmember(c->win, netatom[NetWMState], netatom[NetWMFullscreen], true);

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
    setatomlistmember(c->win, netatom[NetWMState], netatom[NetWMFullscreen], false);

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

  updateclientcomposition(c);

}

static void
setlayout(const Arg *arg)
{
  if (   !arg
      || !arg->v
      ||  arg->v != selmon->lt[selmon->sellt]
      )
  {
    selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
    selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
  }
  if (arg  &&  arg->v)
  {
    selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] =
      (Layout *)arg->v;
  }

  selmon->lt[selmon->sellt] =
    selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];

  if (selmon->sel)
    arrange(selmon);

  publishfeedback("layout", selmon, layoutid(selmon->lt[selmon->sellt]), 0);
}

/* arg > 1.0 will set mfact absolutely */
static void
setmfact(const Arg *arg)
{
  float f;

  if (!arg  ||  !selmon->lt[selmon->sellt]->arrange)
    return;

  f = (arg->f < 1.0)
    ? (arg->f + selmon->mfact)
    : (arg->f - 1.0);

  if (f < 0.1 || f > 0.9)
    return;

  selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
  arrange(selmon);
  publishfeedback("master-factor", selmon,
                  (int32_t)(selmon->mfact * RAWM_CONTROL_FACTOR_SCALE), 0);
}

static void
setup(void)
{
  XSetWindowAttributes wa;

  if (pipe(signalpipe) < 0 ||
      setsignalpipeflags(signalpipe[0]) < 0 ||
      setsignalpipeflags(signalpipe[1]) < 0)
    die("fatal: could not create signal pipe: %s\n", strerror(errno));

  if (installsignalhandler(SIGHUP, sighup) < 0 ||
      installsignalhandler(SIGTERM, sigterm) < 0 ||
      installsignalhandler(SIGINT, sigterm) < 0)
    die("fatal: could not install signal handler: %s\n", strerror(errno));

  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);

  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  updategeom();

  wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", false);
  wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", false);
  wmatom[WMState] = XInternAtom(dpy, "WM_STATE", false);
  wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", false);

  netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", false);
  netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", false);
  netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", false);
  netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", false);
  netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", false);
  netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", false);
  netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", false);
  netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", false);
  netatom[NetWMStrut] = XInternAtom(dpy, "_NET_WM_STRUT", false);
  netatom[NetWMStrutPartial] = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", false);
  utf8atom = XInternAtom(dpy, "UTF8_STRING", false);
  dockatom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", false);
  surfaceatom = XInternAtom(dpy, "_RAWM_SURFACE", false);
  rawmatom[RawmAtomFloating] = XInternAtom(dpy, "_RAWM_FLOATING", false);
  rawmatom[RawmAtomSelected] = XInternAtom(dpy, "_RAWM_SELECTED", false);
  rawmatom[RawmAtomLayout] = XInternAtom(dpy, "_RAWM_LAYOUT", false);
  rawmatom[RawmAtomMonitor] = XInternAtom(dpy, "_RAWM_MONITOR", false);
  if (rawm_control_init(&control, dpy, root, screen, utf8atom, stderr) < 0)
    die("fatal: could not initialize rawm control plane\n");

  (void)updateworkareas();

  cursor[CurNormal] = XCreateFontCursor(dpy, XC_left_ptr);
  cursor[CurResize] = XCreateFontCursor(dpy, XC_sizing);
  cursor[CurMove] = XCreateFontCursor(dpy, XC_fleur);

  for (int i = 0; i < RAWM_COLOR_SCHEMES; ++i)
    dc.colors[i][ColBorder] = getcolor(config.colors[i]);

  dc.gc = XCreateGC(dpy, root, 0, NULL);
  XSetLineAttributes(dpy, dc.gc, 1, LineSolid, CapButt, JoinMiter);

  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)netatom, NetLast);
  XDeleteProperty(dpy, root, netatom[NetClientList]);

  wa.cursor = cursor[CurNormal];
  wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                  ButtonPressMask | PointerMotionMask | EnterWindowMask |
                  StructureNotifyMask | PropertyChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
}

static void
showhide(Client *c)
{
  if (!c)
    return;

  if (ISVISIBLE(c))
  {
    /* show clients top down */
    XMoveWindow(dpy, c->win, clientdisplayx(c), c->y);

    showhide(c->snext);
  }
  else
  {
    /* hide clients bottom up */
    showhide(c->snext);
    XMoveWindow(dpy, c->win, clientdisplayx(c), c->y);
  }
}

static int
setsignalpipeflags(int fd)
{
  int flags;

  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return -1;
  flags = fcntl(fd, F_GETFD, 0);
  if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
    return -1;
  return 0;
}

static int
installsignalhandler(int signum, void (*handlerfn)(int))
{
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = handlerfn;
  sigemptyset(&action.sa_mask);
  return sigaction(signum, &action, NULL);
}

static void
wakesignalpipe(int signum)
{
  unsigned char byte = (unsigned char)signum;
  int saved_errno = errno;

  if (signalpipe[1] >= 0)
    (void)write(signalpipe[1], &byte, 1);
  errno = saved_errno;
}

static void
drainsignalpipe(void)
{
  unsigned char bytes[64];

  while (read(signalpipe[0], bytes, sizeof(bytes)) > 0)
    /* NOTHING */;
}

static void
sighup(int sig)
{
  restart = 1;
  running = 0;
  wakesignalpipe(sig);
}

static void
sigterm(int sig)
{
  running = 0;
  wakesignalpipe(sig);
}

static void
tag(const Arg *arg)
{
  if (   selmon->sel
      && arg->ui & TAGMASK)
  {
    unsigned int tags = arg->ui & TAGMASK;

    selmon->sel->tags = tags;
    focus(NULL);
    arrange(selmon);
    publishcontrolstate();
    publishfeedback("tag", selmon, (int32_t)tags, 0);
  }
}

static void
tagmon(const Arg *arg)
{
  Client *c;
  Monitor *target;

  if (!selmon->sel  ||  !mons->next)
    return;

  c = selmon->sel;
  target = dirtomon(arg->i);
  if (target == c->mon)
    return;

  sendmon(c, target);
  publishfeedback("tag-monitor", target, target->num, 0);
}


static void
tile(Monitor *m)
{
  LayoutArea area;
  int      i, n, h, mw, my, ty;
  int      master, stack, mgap, sgap, splitgap, sw;
  Client  *c;

  for (n = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), n++)
    /* NOTHING */;

  if (n == 0)
    return;

  area = layoutarea(m, n);
  master = MIN(n, m->nmaster);
  stack = n - master;
  mgap = fitgap(config.gap_inner, area.h, master);
  sgap = fitgap(config.gap_inner, area.h, stack);
  splitgap = (master && stack) ? fitgap(config.gap_inner, area.w, 2) : 0;

  if (n > m->nmaster)
    mw = m->nmaster ? ((area.w - splitgap) * m->mfact) : 0;
  else
    mw = area.w;
  sw = area.w - mw - splitgap;

  for (i = my = ty = 0, c = nexttiled(m->clients);
       c;
       c = nexttiled(c->next), i++)
  {
    if (i < m->nmaster)
    {
      h = (area.h - my - mgap * (master - i - 1)) / (master - i);
      resize(c,
             area.x,
             area.y + my,
             mw - (2 * c->bw),
             h  - (2 * c->bw),
             false
             );
      my += HEIGHT(c) + mgap;
    }
    else
    {
      h = (area.h - ty - sgap * (n - i - 1)) / (n - i);
      resize(c,
             area.x + mw + splitgap,
             area.y + ty,
             sw - (2 * c->bw),
                      h - (2 * c->bw),
             false
             );
      ty += HEIGHT(c) + sgap;
    }
  }
}

static void
togglefloating(const Arg *arg)
{
  Client *c = selmon->sel;

  if (!c || c->isfullscreen)
    return;

  c->isfloating = !c->isfloating || c->isfixed;

  if (c->isfloating)
  {
    if (c->bw != (int)config.border_width)
    {
      c->oldbw = c->bw;
      c->bw = (int)config.border_width;
      XSetWindowBorderWidth(dpy, c->win, c->bw);
      configure(c);
    }
    restorefreegeometry(c);
  }

  arrange(c->mon);

  if (arg)
    publishfeedback("floating", c->mon, clientiselevated(c) ? 1 : 0, 0);
}

static void
togglefullscr(__attribute__((unused)) const Arg *arg)
{
  if (selmon->sel)
  {
    Client *c = selmon->sel;

    setfullscreen(c, !c->isfullscreen);
    publishfeedback("fullscreen", c->mon, c->isfullscreen ? 1 : 0, 0);
  }
}

static void
toggletag(const Arg *arg)
{
  unsigned int newtags;

  if (!selmon->sel)
    return;

  if ((newtags = selmon->sel->tags ^ (arg->ui & TAGMASK)))
  {
    selmon->sel->tags = newtags;
    focus(NULL);
    arrange(selmon);
    publishcontrolstate();
    publishfeedback("toggle-tag", selmon, (int32_t)newtags, 0);
  }
}

static void
toggleview(const Arg *arg)
{
  unsigned int newtagset =
    selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

  if (!newtagset)
    return;

  if (newtagset == TAGMASK)
  {
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->pertag->curtag  = 0;
  }
  /*
   * curtag 0 represents the all-tags view and has no single bit. Moving
   * away from it must choose one of the remaining viewed tags without
   * evaluating a negative shift.
   */
  else if (   selmon->pertag->curtag == 0
           || !(newtagset & 1u << (selmon->pertag->curtag - 1)))
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


  focus(NULL);
  arrange(selmon);
  publishcontrolstate();
  publishfeedback("view", selmon,
                  (int32_t)(selmon->tagset[selmon->seltags] & TAGMASK), 0);
}

static void
unfocus(Client *c, bool setfocus)
{
  if (!c)
    return;


  grabbuttons(c, false);
  XSetWindowBorder(dpy, c->win, dc.colors[0][ColBorder]);
  setcardinalproperty(c->win, rawmatom[RawmAtomSelected], 0);

  if (setfocus)
  {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }

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

    clearclientcomposition(c);

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
  updateclientlist();
  arrange(m);
}

static void
unmapnotify(XEvent *e)
{
  Client      *c;
  XUnmapEvent *ev = &e->xunmap;
  bool dock = windowisdock(ev->window);

  if ((c = wintoclient(ev->window)))
  {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, false);
  }

  if (dock)
    reflowworkareas();
}

static bool
updateworkareas(void)
{
  Window root_return, parent_return, *children = NULL;
  unsigned int child_count = 0;
  Monitor *m;
  bool dirty = false;

  for (m = mons; m; m = m->next)
  {
    m->wx = m->mx;
    m->wy = m->my;
    m->ww = m->mw;
    m->wh = m->mh;
  }

  if (XQueryTree(dpy, root, &root_return, &parent_return,
                 &children, &child_count))
  {
    unsigned int i;

    for (i = 0; i < child_count; ++i)
    {
      XWindowAttributes wa;
      unsigned long strut[12];

      if (!XGetWindowAttributes(dpy, children[i], &wa) ||
          wa.map_state != IsViewable ||
          !windowisdock(children[i]) ||
          !getdockstrut(children[i], strut))
        continue;

      XSelectInput(dpy, children[i], PropertyChangeMask);

      for (m = mons; m; m = m->next)
      {
        long mx1 = m->mx;
        long my1 = m->my;
        long mx2 = m->mx + m->mw - 1L;
        long my2 = m->my + m->mh - 1L;
        long wx1 = m->wx;
        long wy1 = m->wy;
        long wx2 = m->wx + m->ww;
        long wy2 = m->wy + m->wh;

        if (strut[0] && (long)strut[4] <= my2 &&
            (long)strut[5] >= my1)
          wx1 = MAX(wx1, MIN((long)strut[0], mx2 + 1L));
        if (strut[1] && (long)strut[6] <= my2 &&
            (long)strut[7] >= my1)
          wx2 = MIN(wx2, MAX((long)sw - (long)strut[1], mx1));
        if (strut[2] && (long)strut[8] <= mx2 &&
            (long)strut[9] >= mx1)
          wy1 = MAX(wy1, MIN((long)strut[2], my2 + 1L));
        if (strut[3] && (long)strut[10] <= mx2 &&
            (long)strut[11] >= mx1)
          wy2 = MIN(wy2, MAX((long)sh - (long)strut[3], my1));

        m->wx = (int)wx1;
        m->wy = (int)wy1;
        m->ww = MAX(1, (int)(wx2 - wx1));
        m->wh = MAX(1, (int)(wy2 - wy1));
      }
    }
  }
  if (children)
    XFree(children);

  dirty = true;
  return dirty;
}

static void
reflowworkareas(void)
{
  if (!updateworkareas())
    return;
  arrange(NULL);
}

void
updateclientlist()
{
  Client *c;
  Monitor *m;

  XDeleteProperty(dpy, root, netatom[NetClientList]);
  for (m = mons;  m;  m = m->next)
  {
    for (c = m->clients;  c;  c = c->next)
    {
      XChangeProperty(dpy,
                      root,
                      netatom[NetClientList],
                      XA_WINDOW,
                      32,
                      PropModeAppend,
                      (unsigned char *) &(c->win),
                      1
                      );
    }
  }
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

    for (n = 0, m = mons;  m;  m = m->next, n++)
      /* NOTHING */;

    /* only consider unique geometries as separate screens */
    if (!(unique =
          (XineramaScreenInfo *)
            malloc(sizeof(XineramaScreenInfo) * nn)))
    {
      die("fatal: could not malloc() %u bytes\n",
          sizeof(XineramaScreenInfo) * nn);
    }

    for (i = 0, j = 0; i < nn; i++)
    {
      if (isuniquegeom(unique, j, &info[i]))
        memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    }

    XFree(info);

    nn = j;
    if (n <= nn)
    {
      for (i = 0;  i < (nn - n);  i++)
      {
        /* new monitors available */
        for (m = mons;  m && m->next;  m = m->next)
          /* NOTHING */;
        if (m)
          m->next = createmon(i);
        else
          mons    = createmon(i);
      }

      for (i = 0, m = mons;  i < nn && m;  m = m->next, i++)
        if (   i >= n
            || (   unique[i].x_org  != m->mx
                || unique[i].y_org  != m->my
                || unique[i].width  != m->mw
                || unique[i].height != m->mh )
            )
        {
          dirty  = true;
          m->num = i;
          m->mx  = m->wx = unique[i].x_org;
          m->my  = m->wy = unique[i].y_org;
          m->mw  = m->ww = unique[i].width;
          m->mh  = m->wh = unique[i].height;

        }
    }
    else
    {
      /* less monitors available nn < n */
      for (i = nn;  i < n;  i++)
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
      mons = createmon(0);

    if (   mons->mw != sw
        || mons->mh != sh
        )
    {
      dirty    = true;
      mons->mw = mons->ww = sw;
      mons->mh = mons->wh = sh;
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
  for (int i = 0;  i < 8;  i++)
  {
    for (int j = 0;  j < modmap->max_keypermod;  j++)
    {
      if (   modmap->modifiermap[i * modmap->max_keypermod + j]
          == XKeysymToKeycode(dpy, XK_Num_Lock)
          )
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

  if (size.flags & USPosition)
    c->position_authority = RAWM_POSITION_USER;
  else if (size.flags & PPosition)
    c->position_authority = RAWM_POSITION_PROGRAM;
  else
    c->position_authority = RAWM_POSITION_UNSPECIFIED;

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
                && (c->maxh == c->minh)
                );
}


static void
updateclass(Client *c)
{
  XClassHint hint = {NULL, NULL};

  c->class_name[0] = '\0';
  if (XGetClassHint(dpy, c->win, &hint) && hint.res_class)
    snprintf(c->class_name, sizeof(c->class_name), "%s", hint.res_class);
  if (hint.res_name)
    XFree(hint.res_name);
  if (hint.res_class)
    XFree(hint.res_class);
}

static void
updatetitle(Client *c)
{
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);

  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

static void
updatewindowtype(Client *c)
{
  Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

  c->isdialog = wtype == netatom[NetWMWindowTypeDialog];
  if (c->isdialog)
  {
    if (!c->center_explicit)
      c->center = config.dialog_center;
    c->isfloating = true;
  }

  updateclientcomposition(c);
}

static void
updatewmhints(Client *c)
{
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win)))
  {
    if (   c == selmon->sel
        && wmh->flags & XUrgencyHint
        )
    {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
      c->app_urgent = false;
    }
    else
      c->app_urgent = (wmh->flags & XUrgencyHint) ? true : false;

    c->neverfocus = (wmh->flags & InputHint) ? (!wmh->input) : false;
    XFree(wmh);
  }
}

static void
view(const Arg *arg)
{
  unsigned int tags;

  if (!arg)
    return;

  tags = arg->ui & TAGMASK;
  if (tags == selmon->tagset[selmon->seltags])
    return;

  selmon->seltags ^= 1; /* toggle sel tagset */

  if (tags)
  {
    selmon->pertag->prevtag         = selmon->pertag->curtag;
    selmon->tagset[selmon->seltags] = tags;

    if (tags == TAGMASK)
      selmon->pertag->curtag = 0;
    else
    {
      size_t i;

      for (i = 0; !(tags & (1u << i)); i++)
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


  focus(NULL);
  arrange(selmon);
  publishcontrolstate();
  publishfeedback("view", selmon,
                  (int32_t)(selmon->tagset[selmon->seltags] & TAGMASK), 0);
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
  int     x, y;
  Client *c;

  if (w == root && getrootptr(&x, &y))
    return recttomon(x, y, 1, 1);


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

  for (;;)
  {
    win_c = NULL;
    if (!XQueryTree(dpy, win, &win_r, &win_p, &win_c, &nc))
      return;

    if (win_c)
      XFree(win_c);

    if (win_p == win_r)
      break;

    win = win_p;
  }

  if (!(c = wintoclient(win)))
    return;

  a.ui = c->tags & TAGMASK;
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
      || (   ee->request_code == X_CopyArea
          && ee->error_code   == BadDrawable)
      )
    return 0;

  fprintf(stderr,
          "rawm: fatal error: request code=%d, error code=%d\n",
          ee->request_code,
          ee->error_code
          );

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
  die("rawm: another window manager is already running\n");
  return -1;
}

static void
zoom(__attribute__((unused)) const Arg *arg)
{
  Client *c = selmon->sel;

  if (   !selmon->lt[selmon->sellt]->arrange
      || (   selmon->sel
          && selmon->sel->isfloating )
      )
    return;

  if (c == nexttiled(selmon->clients))
  {
    if (!c  ||  !(c = nexttiled(c->next)))
      return;
  }

  pop(c);
}

int
main(int argc, char *argv[])
{
  const char *config_path = NULL;
  bool check_config = false;
  int i;

  for (i = 1; i < argc; ++i)
  {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version"))
    {
      printf("rawm " VERSION "\n");
      return EXIT_SUCCESS;
    }
    else if (!strcmp(argv[i], "-C") || !strcmp(argv[i], "--check-config"))
      check_config = true;
    else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config"))
    {
      if (++i >= argc)
      {
        fputs("rawm: -c/--config requires a path\n", stderr);
        return EXIT_FAILURE;
      }
      config_path = argv[i];
    }
    else
    {
      fprintf(stderr,
              "usage: rawm [-C] [-c path] [-v]\n");
      return EXIT_FAILURE;
    }
  }

  rawm_config_init(&config);
  if (rawm_config_load(&config, config_path, stderr) < 0 ||
      initruntimeconfig(stderr) < 0)
  {
    rawm_config_destroy(&config);
    return EXIT_FAILURE;
  }

  if (check_config)
  {
    freeruntimeconfig();
    rawm_config_destroy(&config);
    return EXIT_SUCCESS;
  }

  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    fputs("warning: no locale support\n", stderr);

  if (!(dpy = XOpenDisplay(NULL)))
  {
    fputs("rawm: cannot open display\n", stderr);
    freeruntimeconfig();
    rawm_config_destroy(&config);
    return EXIT_FAILURE;
  }

  checkotherwm();
  setup();
  scan();
  publishcontrolstate();
  run();
  if (restart)
  {
    execvp(argv[0], argv);
  }
  cleanup();
  XCloseDisplay(dpy);
  freeruntimeconfig();
  rawm_config_destroy(&config);

  return EXIT_SUCCESS;
}

/* vim: sw=2 ts=2 sts=2 et cc=72 tw=70
 * End of file. */
