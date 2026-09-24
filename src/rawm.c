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
 * Each child of the root window is called a client, except windows
 * which have set the override_redirect flag.  Clients are organized
 * in a linked client list on each monitor, the focus history is
 * remembered through a stack list on each monitor.  Each client
 * contains a bit array to indicate the tags of a client.
 *
 * Keys, rules, appearance, and policy are loaded from rawm.conf.
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
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "rawm-config.h"

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
 * @brief Calculate the width of text plus padding.
 * @param X The text string.
 * @return The calculated width.
 */
#define TEXTW(X)   (textnw(X, strlen(X)) + dc.font.height)

/** System Tray defines. */
#define SYSTEM_TRAY_REQUEST_DOCK    0
#define _NET_SYSTEM_TRAY_ORIENTATION_HORZ 0

#ifdef SYSTRAY
/* XEMBED messages. */
# define XEMBED_EMBEDDED_NOTIFY     0
# define XEMBED_WINDOW_ACTIVATE     1
# define XEMBED_FOCUS_IN            4
# define XEMBED_MODALITY_ON         10

# define XEMBED_MAPPED             (1 << 0)
# define XEMBED_WINDOW_ACTIVATE     1
# define XEMBED_WINDOW_DEACTIVATE   2

# define VERSION_MAJOR              0
# define VERSION_MINOR              0
# define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR
#endif /* SYSTRAY */

/** Max tag length including number and semicolon. */
#define MAX_TAGLEN 25

/** Max tag name length (excluding number and semicolon (3)). */
#define MAX_TAGNAMELEN (MAX_TAGLEN - 3)

/** Maximum rawm-v2 property and segment identifier sizes. */
#define RAWM_STATUS_V2_MAX 4096u
#define RAWM_STATUS_ID_MAX 64u

/*********************************************************************
 * Enums & Typedefs.
 */

/** Cursor types used in rawm. */
enum {
  CurNormal, /**< Normal pointer cursor. */
  CurHand,   /**< Interactive panel surface cursor. */
  CurResize, /**< Resize cursor. */
  CurMove,   /**< Move cursor. */
  CurLast    /**< Sentinel value for the last cursor type. */
};

/** Color definitions for drawing. */
enum {
  ColBorder, /**< Border color. */
  ColFG,     /**< Foreground color. */
  ColBG,     /**< Background color. */
  ColLast    /**< Sentinel value for the last color type. */
};

/** Configured color schemes. */
enum {
  SchemeNorm,
  SchemeSel,
  SchemeUrg,
  SchemeErr,
  SchemeFloating
};

typedef enum {
  StatusNormal,
  StatusWarning,
  StatusCritical
} StatusState;

typedef struct {
  char *id;
  char *text;
  StatusState state;
} StatusSegment;

typedef struct {
  StatusSegment *segments;
  size_t count;
} StatusData;

typedef struct {
  unsigned int occupied;
  unsigned int urgent;
  unsigned int viewed;
} TagBarState;

/** EWMH atoms used for window properties and communication. */
enum {
  NetSupported,             /**< _NET_SUPPORTED atom. */
#ifdef SYSTRAY
  NetSystemTray,            /**< _NET_SYSTEM_TRAY_S0 atom. */
  NetSystemTrayOP,          /**< _NET_SYSTEM_TRAY_OPCODE atom. */
  NetSystemTrayOrientation, /**< _NET_SYSTEM_TRAY_ORIENTATION atom. */
#endif /* SYSTRAY */
  NetWMName,                /**< _NET_WM_NAME atom. */
  NetWMState,               /**< _NET_WM_STATE atom. */
  NetWMFullscreen,          /**< _NET_WM_STATE_FULLSCREEN atom. */
  NetActiveWindow,          /**< _NET_ACTIVE_WINDOW atom. */
  NetClientList,            /**< _NET_CLIENT_LIST atom. */
  NetWMWindowType,          /**< _NET_WM_WINDOW_TYPE atom. */
  NetWMWindowTypeDialog,    /**< _NET_WM_WINDOW_TYPE_DIALOG atom. */
  NetLast                   /**< Sentinel value for the last EWMH atom. */
};

#ifdef SYSTRAY
/** Xembed atoms used for system tray communication. */
enum {
  Manager,    /**< MANAGER atom. */
  Xembed,     /**< _XEMBED atom. */
  XembedInfo, /**< _XEMBED_INFO atom. */
  XLast       /**< Sentinel value for the last Xembed atom. */
};
#endif /* SYSTRAY */

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

/** Bar click areas for button bindings. */
enum {
  ClkTagBar,     /**< Click on a tag button. */
  ClkLtSymbol,   /**< Click on the layout symbol. */
  ClkStatusText, /**< Click on the status text area. */
#ifdef WINTITLE
  ClkWinTitle,   /**< Click on the window title area. */
#endif /* WINTITLE */
  ClkClientWin,  /**< Click on a client window. */
  ClkRootWin,    /**< Click on the root window. */
  ClkLast        /**< Sentinel value for the last click area. */
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
  unsigned int click;           /**< The click area (enum Clk...). */
  unsigned int mask;            /**< Modifier mask (e.g., MODKEY). */
  unsigned int button;          /**< Mouse button (e.g., Button1). */
  const char *status_id;        /**< Optional rawm-v2 segment selector. */
  void (*func)(const Arg *arg); /**< Function to execute. */
  Arg arg;                      /**< Argument for the function. */
} Button;

typedef struct {
  unsigned int click;           /**< Resolved click area. */
  Arg arg;                      /**< Dynamic click argument, e.g. tag mask. */
  const char *status_id;        /**< rawm-v2 segment under the pointer. */
} BarHit;

/* Forward declarations. */
typedef struct Monitor  Monitor;
typedef struct Client   Client;

typedef struct {
  Monitor *mon;                 /**< Bar currently under the pointer. */
  unsigned int click;           /**< Interactive area currently hovered. */
  unsigned int tagmask;         /**< Dynamic tag identity for ClkTagBar. */
  char status_id[RAWM_STATUS_ID_MAX + 1]; /**< Stable status identity. */
} BarHover;

typedef struct {
  Window win;                   /**< Override-redirect feedback surface. */
  Pixmap pixmap;                /**< Backing pixmap for expose redraws. */
  Monitor *mon;                 /**< Monitor owning the current feedback. */
  unsigned int width, height;   /**< Current OSD geometry. */
  uint64_t deadline_ms;         /**< Monotonic expiry deadline. */
  bool visible;                 /**< Whether the surface is currently mapped. */
  char text[256];               /**< Current UTF-8 feedback text. */
} RawmOSD;

typedef struct {
  int x, y, w, h;
} RawmGeometry;

typedef enum {
  RAWM_POSITION_UNSPECIFIED,
  RAWM_POSITION_PROGRAM,
  RAWM_POSITION_USER
} RawmPositionAuthority;

/**
 * @brief Client structure to manage individual windows.
 */
struct Client {
  char name[256];       /**< Window title. */
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
#ifdef PWKL
  unsigned char kbdgrp; /**< Keyboard group for per-window layout. */
#endif /* PWKL */
};

/**
 * @brief Drawing context.
 *
 * Holds information and resources needed for drawing on the bar.
 */
typedef struct {
  int x, y, w, h;    /**< Current drawing area geometry. */
  XftColor colors[MAXCOLORS][ColLast]; /**< Runtime color schemes. */
  Drawable drawable; /**< Pixmap for double buffering. */
  GC gc;             /**< Graphics context. */
  struct {
    int ascent;     /**< Font ascent. */
    int descent;    /**< Font descent. */
    int height;     /**< Font height (ascent + descent). */
    XftFont *xfont; /**< Xft font. */
  } font; /**< Font information. */
} DC;

/**
 * @brief Key definition for keyboard bindings.
 */
typedef struct {
  unsigned int mod; /**< Modifier mask (e.g., MODKEY). */
  KeySym keysym;    /**< Key symbol (e.g., XK_Return). */
  void (*func)(const Arg *); /**< Function to execute. */
  Arg arg;          /**< Argument for the function. */
} Key;

/**
 * @brief Layout definition.
 *
 * Defines a layout symbol and the corresponding arrangement function.
 */
typedef struct {
  const char *symbol; /**< Symbol displayed in the bar (e.g., "[]="). */
  void (*arrange)(Monitor *); /**< Function to arrange clients on a monitor (e.g., tile, monocle). NULL means floating. */
} Layout;

/* Forware declaration for Pertag structure. */
typedef struct Pertag Pertag;

/**
 * @brief Monitor structure to manage screens.
 */
struct Monitor {
  char ltsymbol[16];      /**< Layout symbol string displayed in the bar. */
  char tag_names[RAWM_MAX_TAGS][MAX_TAGLEN]; /**< Runtime tag labels. */
  float mfact;            /**< Master area factor [0.05..0.95] (per tag). */
  int nmaster;            /**< Number of clients in master area (per tag). */
  int num;                /**< Monitor number (0, 1, ...). */
  int by;                 /**< Bar Y-coordinate. */
  int mx, my, mw, mh;     /**< Monitor geometry (full screen size). */
  int wx, wy, ww, wh;     /**< Work area geometry (excluding bar). */
  unsigned int seltags;   /**< Index of the currently selected tagset (0 or 1). */
  unsigned int sellt;     /**< Index of the currently selected layout (0 or 1). */
  unsigned int tagset[2]; /**< Array holding current and previous tag masks. */
  bool showbar;           /**< Whether the bar is visible on this monitor (per tag). */
  bool topbar;            /**< Whether the bar is at the top (global config). */
  Client *clients;        /**< List of clients on this monitor. */
  Client *sel;            /**< Currently selected client on this monitor. */
  Client *stack;          /**< Stacked list of clients on this monitor (focus history). */
  Monitor *next;          /**< Next monitor in the global monitor list. */
  Window barwin;          /**< Window ID of the status bar for this monitor. */
  const Layout *lt[2];    /**< Array holding current and previous layout (per tag). */
  Pertag *pertag;         /**< Pointer to the per-tag configuration for this monitor. */
};

#ifdef SYSTRAY
/**
 * @brief Systray structure.
 *
 * Manages the system tray window and its icons.
 */
typedef struct Systray Systray;
struct Systray {
  Window win;    /**< Systray window ID. */
  Client *icons; /**< List of systray icons (treated as clients). */
};
#endif /* SYSTRAY */

/*********************************************************************
 * Function declarations.
 */

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
static BarHit         barhitat(Monitor *m, int x);
static bool           barhitbound(const BarHit *hit);
static bool           barhovermatches(const Monitor *m, unsigned int click,
                                      unsigned int tagmask,
                                      const char *status_id);
static void           clearbarhover(void);
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
static void           configure(Client *c);
static void           configurenotify(XEvent *e);
static void           configurerequest(XEvent *e);
static Monitor       *createmon(int idx);
static void           destroynotify(XEvent *e);
static void           detach(Client *c);
static void           detachstack(Client *c);
static void           die(const char *errstr, ...);
static Monitor       *dirtomon(int dir);
static void           drawbar(Monitor *m);
static void           drawbarindicator(const Monitor *m, int x, int width,
                                       XftColor col[ColLast]);
static void           drawbars(void);
static void           drawstatus(int x, int right);
static TagBarState    tagbarstate(const Monitor *m);
static bool           tagbarvisible(const TagBarState *state, unsigned int tag);
static void           drawtext(const char *text, XftColor col[ColLast],
                               bool pad);
static void           drawtextcell(const char *text, XftColor col[ColLast],
                                   bool pad);
static void           drawtextcentered(const char *text,
                                       XftColor col[ColLast], bool pad);
static void           drawtextaligned(const char *text,
                                      XftColor col[ColLast], bool pad,
                                      bool centered, unsigned int margin);
static void           enternotify(XEvent *e);
static void           expose(XEvent *e);
static void           focus(Client *c);
static void           focusin(XEvent *e);
static void           focusmon(const Arg *arg);
static void           focusnstack(const Arg *arg);
static void           focusstack(const Arg *arg);
static void           grid(Monitor *m);
static bool           atomlistcontains(Window w, Atom prop, Atom atom);
static void           setatomlistmember(Window w, Atom prop, Atom atom, bool present);
static void           setsurfaceproperties(Window window, const char *instance,
                                           const char *surface,
                                           unsigned long monitor);
static bool           clientiselevated(const Client *c);
static int            clientselectedscheme(const Client *c);
static void           clearclientcomposition(Client *c);
static void           updateclientcomposition(Client *c);

#ifdef SYSTRAY
static Atom           getatomprop(Client *c, Atom prop);
static unsigned int   getsystraywidth();
static void           removesystrayicon(Client *i);
static void           resizebarwin(Monitor *m);
static void           resizerequest(XEvent *e);
static void           updatesystray(void);
static void           updatesystrayicongeom(Client *i, int w, int h);
static void           updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static Client        *wintosystrayicon(Window w);
#endif /* SYSTRAY */

static XftColor       getcolor(const char *colstr);
static bool           getrootptr(int *x, int *y);
static long           getstate(Window w);
static bool           validutf8(const unsigned char *text, size_t length);
static void           freestatus(StatusData *status);
static bool           getstatusprop(StatusData *status);
static int            statusscheme(StatusState state);
static const char    *statusidat(int x, int start, int right);
static int            statuswidth(void);
static bool           gettextprop(Window w, Atom atom, char *text,
                                  unsigned int size);
static void           grabbuttons(Client *c, bool focused);
static void           grabkeys(void);
static void           incnmaster(const Arg *arg);
static void           initfont(const char *fontstr);
static void           keypress(XEvent *e);
static void           killclient(const Arg *arg);
static void           leavenotify(XEvent *e);
static void           manage(Window w, XWindowAttributes *wa);
static void           mappingnotify(XEvent *e);
static void           maprequest(XEvent *e);
static void           monocle(Monitor *m);
static void           motionnotify(XEvent *e);
static uint64_t       monotonicms(void);
static void           movemouse(const Arg *arg);
static void           nametag(const Arg *arg);
static void           osdcreate(void);
static void           osddraw(void);
static void           osdexpire(void);
static void           osdhide(void);
static int            osdpolltimeout(void);
static void           osdshow(Monitor *m, const char *text);
static void           osdshowf(Monitor *m, const char *fmt, ...);
static void           osdshowview(Monitor *m);
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

#ifdef SYSTRAY
static bool           sendevent(Window w, Atom proto, int m, long d0,
                                long d1, long d2, long d3, long d4);
#else
static bool           sendevent(Client *c, Atom proto);
#endif /* SYSTRAY */

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
static void           updateclientlist(void);
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
static int            initruntimeconfig(FILE *err);
static void           freeruntimeconfig(void);
static unsigned int   runtime_modifiers(unsigned int modifiers);

/*********************************************************************
 * Global variables.
 */

#ifdef SYSTRAY
static Systray *systray = NULL;
static unsigned long systrayorientation = _NET_SYSTEM_TRAY_ORIENTATION_HORZ;
#endif /* SYSTRAY */

static const char     broken[] = "broken";
static StatusData     statusdata = {0};
static Atom           dockatom, statusatom, surfaceatom, utf8atom;
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
  [LeaveNotify]       = leavenotify,
  [MappingNotify]     = mappingnotify,
  [MapRequest]        = maprequest,
  [MotionNotify]      = motionnotify,
  [PropertyNotify]    = propertynotify,
#ifdef SYSTRAY
  [ResizeRequest]     = resizerequest,
#endif /* SYSTRAY */
  [UnmapNotify]       = unmapnotify
};

#ifdef SYSTRAY
static Atom           wmatom[WMLast], netatom[NetLast], xatom[XLast];
#else
static Atom           wmatom[WMLast], netatom[NetLast];
#endif /* SYSTRAY */
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
  { NULL, tile },
  { NULL, NULL },
  { NULL, monocle },
  { NULL, bstack },
  { NULL, bstackhoriz },
  { NULL, grid },
};
static Key           *keys = NULL;
static size_t         key_count = 0;
static Button        *buttons = NULL;
static size_t         button_count = 0;
static BarHover       barhover = { .click = ClkRootWin };
static RawmOSD        osd = { .win = None, .pixmap = None };
static unsigned int   opacity = 0xffffffffu;

struct Pertag {
  unsigned int  curtag, prevtag;     /* current and previous tag           */
  int           nmasters[RAWM_MAX_TAGS + 1];  /* number of windows in master area   */
  float         mfacts[RAWM_MAX_TAGS + 1];    /* mfacts per tag                     */
  unsigned int  sellts[RAWM_MAX_TAGS + 1];    /* selected layouts                   */
  const Layout *ltidxs[RAWM_MAX_TAGS + 1][2]; /* matrix of tags and layouts indexes */
  bool          showbars[RAWM_MAX_TAGS + 1];  /* display bar for the current tag    */
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
action_function(RawmAction action))(const Arg *)
{
  switch (action)
  {
    case RAWM_ACTION_SPAWN:             return spawn;
    case RAWM_ACTION_TOGGLE_BAR:        return togglebar;
    case RAWM_ACTION_RENAME_TAG:        return nametag;
    case RAWM_ACTION_FOCUS_STACK:       return focusstack;
    case RAWM_ACTION_FOCUS_INDEX:       return focusnstack;
    case RAWM_ACTION_INC_MASTER:        return incnmaster;
    case RAWM_ACTION_SET_MASTER_FACTOR: return setmfact;
    case RAWM_ACTION_ZOOM:              return zoom;
    case RAWM_ACTION_VIEW:              return view;
    case RAWM_ACTION_TOGGLE_VIEW:       return toggleview;
    case RAWM_ACTION_KILL_CLIENT:       return killclient;
    case RAWM_ACTION_SET_LAYOUT:        return setlayout;
    case RAWM_ACTION_TOGGLE_FLOATING:   return togglefloating;
    case RAWM_ACTION_TAG:               return tag;
    case RAWM_ACTION_TOGGLE_TAG:        return toggletag;
    case RAWM_ACTION_WINDOW_VIEW:       return winview;
    case RAWM_ACTION_TOGGLE_FULLSCREEN: return togglefullscr;
    case RAWM_ACTION_FOCUS_MONITOR:     return focusmon;
    case RAWM_ACTION_TAG_MONITOR:       return tagmon;
    case RAWM_ACTION_QUIT:              return quit;
    case RAWM_ACTION_MOVE:              return movemouse;
    case RAWM_ACTION_RESIZE:            return resizemouse;
  }
  return NULL;
}

static Arg
runtime_argument(const RawmBinding *binding)
{
  Arg arg = {0};
  const RawmCommand *command;

  switch (binding->action)
  {
    case RAWM_ACTION_SPAWN:
      command = rawm_config_find_command(&config, binding->command);
      arg.v = command ? command->argv : NULL;
      break;
    case RAWM_ACTION_FOCUS_STACK:
    case RAWM_ACTION_FOCUS_INDEX:
    case RAWM_ACTION_INC_MASTER:
    case RAWM_ACTION_FOCUS_MONITOR:
    case RAWM_ACTION_TAG_MONITOR:
      arg.i = binding->number;
      break;
    case RAWM_ACTION_SET_MASTER_FACTOR:
      arg.f = (float)(binding->factor_absolute
                      ? binding->factor + 1.0
                      : binding->factor);
      break;
    case RAWM_ACTION_VIEW:
    case RAWM_ACTION_TOGGLE_VIEW:
    case RAWM_ACTION_TAG:
    case RAWM_ACTION_TOGGLE_TAG:
      if (binding->has_tag)
        arg.ui = binding->all_tags ? ~0u : 1u << (binding->tag - 1);
      break;
    case RAWM_ACTION_SET_LAYOUT:
      if (binding->has_layout)
        arg.v = &layouts[binding->layout];
      break;
    case RAWM_ACTION_QUIT:
      arg.i = binding->restart ? 1 : 0;
      break;
    default:
      break;
  }
  return arg;
}

static unsigned int
runtime_click(RawmClick click)
{
  switch (click)
  {
    case RAWM_CLICK_TAG_BAR: return ClkTagBar;
    case RAWM_CLICK_LAYOUT:  return ClkLtSymbol;
    case RAWM_CLICK_STATUS:  return ClkStatusText;
#ifdef WINTITLE
    case RAWM_CLICK_TITLE:   return ClkWinTitle;
#else
    case RAWM_CLICK_TITLE:   return ClkLast;
#endif
    case RAWM_CLICK_CLIENT:  return ClkClientWin;
    case RAWM_CLICK_ROOT:    return ClkRootWin;
  }
  return ClkLast;
}

static int
initruntimeconfig(FILE *err)
{
  size_t i, j;

  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i)
    layouts[i].symbol = config.layout_symbols[i];

  keys = calloc(config.key_count, sizeof(*keys));
  buttons = calloc(config.button_count, sizeof(*buttons));
  if ((config.key_count && !keys) || (config.button_count && !buttons))
  {
    fputs("rawm: out of memory while building input configuration\n", err);
    freeruntimeconfig();
    return -1;
  }
  key_count = config.key_count;
  button_count = config.button_count;

  for (i = 0; i < key_count; ++i)
  {
    keys[i].mod = runtime_modifiers(config.keys[i].modifiers);
    keys[i].keysym = XStringToKeysym(config.keys[i].key);
    if (keys[i].keysym == NoSymbol)
    {
      fprintf(err, "rawm: configuration error: unknown key symbol '%s'\n",
              config.keys[i].key);
      freeruntimeconfig();
      return -1;
    }
    keys[i].func = action_function(config.keys[i].binding.action);
    keys[i].arg = runtime_argument(&config.keys[i].binding);
    for (j = 0; j < i; ++j)
    {
      if (keys[j].mod == keys[i].mod && keys[j].keysym == keys[i].keysym)
      {
        fprintf(err, "rawm: configuration error: duplicate resolved key binding '%s'\n",
                config.keys[i].key);
        freeruntimeconfig();
        return -1;
      }
    }
  }

  for (i = 0; i < button_count; ++i)
  {
    buttons[i].click = runtime_click(config.buttons[i].click);
    if (buttons[i].click == ClkLast)
    {
      fputs("rawm: configuration error: title click bindings require WINTITLE support\n",
            err);
      freeruntimeconfig();
      return -1;
    }
    buttons[i].mask = runtime_modifiers(config.buttons[i].modifiers);
    buttons[i].button = config.buttons[i].button;
    buttons[i].status_id = config.buttons[i].status_id;
    buttons[i].func = action_function(config.buttons[i].binding.action);
    buttons[i].arg = runtime_argument(&config.buttons[i].binding);
  }

  opacity = (unsigned int)(config.opacity * 4294967295.0);
  return 0;
}

static void
freeruntimeconfig(void)
{
  free(keys);
  free(buttons);
  keys = NULL;
  buttons = NULL;
  key_count = 0;
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

  if (*h < bh)
    *h = bh;

  if (*w < bh)
    *w = bh;

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

  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);

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
                       dc.colors[clientselectedscheme(c)][ColBorder].pixel);
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

static BarHit
barhitat(Monitor *m, int x)
{
  BarHit hit = { .click = ClkRootWin };
  TagBarState state;
  unsigned int i;
  int cursor;
  bool drew_tag = false;

  if (!m)
    return hit;

  state = tagbarstate(m);
  cursor = (int)config.bar_padding;

  for (i = 0; i < config.tag_count; ++i)
  {
    int width;

    if (!tagbarvisible(&state, i))
      continue;
    if (drew_tag)
      cursor += (int)config.bar_item_spacing;

    width = TEXTW(m->tag_names[i]);
    if (x >= cursor && x < cursor + width)
    {
      hit.click = ClkTagBar;
      hit.arg.ui = 1u << i;
      return hit;
    }

    cursor += width;
    drew_tag = true;
  }

  if (drew_tag)
    cursor += (int)config.bar_section_spacing;

  if (x >= cursor && x < cursor + blw)
  {
    hit.click = ClkLtSymbol;
    return hit;
  }

  {
    int status_x;
    int status_right = m->ww - (int)config.bar_padding;
    int status_w = m == selmon ? statuswidth() : 0;
    int title_x = cursor + blw + (int)config.bar_section_spacing;

#ifdef SYSTRAY
    if (config.show_systray && m == selmon)
    {
      status_right -= getsystraywidth();
      status_right -= (int)config.bar_section_spacing;
    }
#endif /* SYSTRAY */

    status_x = status_right - status_w;
    if (status_x < title_x)
      status_x = title_x;

    if (status_w && x >= status_x && x < status_right)
    {
      hit.click = ClkStatusText;
      hit.status_id = statusidat(x, status_x, status_right);
      return hit;
    }

#ifdef WINTITLE
    if (x >= title_x && x < (status_w ? status_x : status_right))
      hit.click = ClkWinTitle;
#endif /* WINTITLE */
  }

  return hit;
}

static bool
barhitbound(const BarHit *hit)
{
  size_t i;

  if (!hit || hit->click == ClkRootWin)
    return false;

  for (i = 0; i < button_count; ++i)
  {
    if (buttons[i].click != hit->click || !buttons[i].func)
      continue;

    if (hit->click == ClkStatusText && buttons[i].status_id)
    {
      if (!hit->status_id || strcmp(buttons[i].status_id, hit->status_id))
        continue;
    }
    return true;
  }

  return false;
}

static bool
barhovermatches(const Monitor *m, unsigned int click, unsigned int tagmask,
                const char *status_id)
{
  if (barhover.mon != m || barhover.click != click)
    return false;

  if (click == ClkTagBar)
    return barhover.tagmask == tagmask;
  if (click == ClkStatusText)
    return status_id && barhover.status_id[0] &&
           !strcmp(barhover.status_id, status_id);
  return true;
}

static void
clearbarhover(void)
{
  Monitor *m = barhover.mon;

  if (!m)
    return;

  barhover.mon = NULL;
  barhover.click = ClkRootWin;
  barhover.tagmask = 0;
  barhover.status_id[0] = '\0';

  XDefineCursor(dpy, m->barwin, cursor[CurNormal]);
  drawbar(m);
}

static void
buttonpress(XEvent *e)
{
  Arg                  arg   = {0};
  Client              *c;
  Monitor             *m;
  XButtonPressedEvent *ev    = &e->xbutton;
  unsigned int         click = ClkRootWin;
  const char          *status_id = NULL;

  /* focus monitor if necessary */
  if (  (m = wintomon(ev->window))
      && m != selmon
      )
  {
    unfocus(selmon->sel, true);
    selmon = m;
    focus(NULL);
  }

  if (ev->window == selmon->barwin)
  {
    BarHit hit = barhitat(selmon, ev->x);

    click = hit.click;
    arg = hit.arg;
    status_id = hit.status_id;
  }
  else if ((c = wintoclient(ev->window)))
  {
    focus(c);
    click = ClkClientWin;
  }

  {
    Button *fallback = NULL;
    bool status_specific = false;

    for (size_t i = 0; i < button_count; i++)
    {
      if (   click                      != buttons[i].click
          || !buttons[i].func
          || buttons[i].button          != ev->button
          || CLEANMASK(buttons[i].mask) != CLEANMASK(ev->state)
          )
        continue;

      if (click == ClkStatusText)
      {
        if (buttons[i].status_id)
        {
          if (status_id && !strcmp(buttons[i].status_id, status_id))
          {
            buttons[i].func(&buttons[i].arg);
            status_specific = true;
          }
        }
        else
          fallback = &buttons[i];
        continue;
      }

      buttons[i].func(
        (click == ClkTagBar && buttons[i].arg.i == 0)
          ? &arg
          : &buttons[i].arg
          );
    }

    if (click == ClkStatusText && !status_specific && fallback)
      fallback->func(&fallback->arg);
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

  for (m = mons;  m;  m = m->next)
  {
    while (m->stack)
      unmanage(m->stack, false);
  }

  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  if (osd.pixmap != None)
    XFreePixmap(dpy, osd.pixmap);
  if (osd.win != None)
    XDestroyWindow(dpy, osd.win);
  XFreePixmap(dpy, dc.drawable);
  XFreeGC(dpy, dc.gc);
  XFreeCursor(dpy, cursor[CurNormal]);
  XFreeCursor(dpy, cursor[CurHand]);
  XFreeCursor(dpy, cursor[CurResize]);
  XFreeCursor(dpy, cursor[CurMove]);

  while (mons)
    cleanupmon(mons);

#ifdef SYSTRAY
  if (config.show_systray)
  {
    XUnmapWindow(dpy, systray->win);
    XDestroyWindow(dpy, systray->win);
    free(systray);
  }
#endif /* SYSTRAY */

  XSync(dpy, false);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  freestatus(&statusdata);
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

  XUnmapWindow(dpy, mon->barwin);
  XDestroyWindow(dpy, mon->barwin);

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

static void
clientmessage(XEvent *e)
{
  XClientMessageEvent *cme = &e->xclient;
  Client              *c   = wintoclient(cme->window);

#ifdef SYSTRAY
  XWindowAttributes    wa;
  XSetWindowAttributes swa;
  if (   config.show_systray
      && cme->window       == systray->win
      && cme->message_type == netatom[NetSystemTrayOP]
      )
  {
    /* add systray icons */
    if (clientmessagecard32(cme, 1) == SYSTEM_TRAY_REQUEST_DOCK)
    {
      if (!(c = (Client *)calloc(1, sizeof(Client))))
        die("fatal: could not malloc() %u bytes\n", sizeof(Client));

      c->win  = (Window)clientmessagecard32(cme, 2);
      c->mon  = selmon;
      c->next = systray->icons;
      systray->icons = c;
      XGetWindowAttributes(dpy, c->win, &wa);
      c->x = c->oldx = c->y = c->oldy = 0;
      c->w = c->oldw = wa.width;
      c->h = c->oldh = wa.height;
      c->oldbw = wa.border_width;
      c->bw = 0;
      c->isfloating = True;
      /* reuse tags field as mapped status */
      c->tags = 1;
      updatesizehints(c);
      updatesystrayicongeom(c, wa.width, wa.height);
      XAddToSaveSet(dpy, c->win);
      XSelectInput(dpy, c->win,
          StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
      XReparentWindow(dpy, c->win, systray->win, 0, 0);
      /* use parents background pixmap */
      swa.background_pixmap = ParentRelative;
      swa.background_pixel  = dc.colors[0][ColBG].pixel;
      XChangeWindowAttributes(dpy, c->win,
          CWBackPixmap | CWBackPixel, &swa);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask,
          CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0 , systray->win,
          XEMBED_EMBEDDED_VERSION);
      /* FIXME not sure if I have to send these events, too */
      sendevent(c->win, netatom[Xembed], StructureNotifyMask,
          CurrentTime, XEMBED_FOCUS_IN, 0 , systray->win,
          XEMBED_EMBEDDED_VERSION);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask,
          CurrentTime, XEMBED_WINDOW_ACTIVATE, 0 , systray->win,
          XEMBED_EMBEDDED_VERSION);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask,
          CurrentTime, XEMBED_MODALITY_ON, 0 , systray->win,
          XEMBED_EMBEDDED_VERSION);
      resizebarwin(selmon);
      updatesystray();
      setclientstate(c, NormalState);
    }
    return;
  }
#endif /* SYSTRAY */
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
      if (dc.drawable != 0)
        XFreePixmap(dpy, dc.drawable);

      dc.drawable = XCreatePixmap(dpy,
                                  root,
                                  sw,
                                  bh,
                                  DefaultDepth(dpy, screen)
                                  );
      updatebars();

      for (m = mons;  m;  m = m->next)
      {
#ifdef SYSTRAY
        resizebarwin(m);
#else
        XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
#endif /* SYSTRAY */
      }
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
  m->showbar    = config.show_bar;
  m->topbar     = config.top_bar;
  m->lt[0]      = &layouts[tagcfg[0].layout];
  m->lt[1]      = &layouts[RAWM_LAYOUT_FLOATING];

  for (i = 0; i < config.tag_count; ++i)
    snprintf(m->tag_names[i], sizeof m->tag_names[i], "%s", tagcfg[i].name);

  snprintf(m->ltsymbol, sizeof m->ltsymbol, "%s", m->lt[0]->symbol);

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
    m->pertag->showbars[i]  = m->showbar;
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
#ifdef SYSTRAY
  else if ((c = wintosystrayicon(ev->window)))
  {
    removesystrayicon(c);
    resizebarwin(selmon);
    updatesystray();
  }
#endif /* SYSTRAY */
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

static TagBarState
tagbarstate(const Monitor *m)
{
  TagBarState state = {
    .viewed = m->tagset[m->seltags] & TAGMASK
  };
  const Client *c;

  for (c = m->clients; c; c = c->next)
  {
    unsigned int tags = c->tags & TAGMASK;

    /* An all-tags client does not make every tag appear occupied. */
    if (tags != TAGMASK)
      state.occupied |= tags;
    if (clienturgent(c))
      state.urgent |= tags;
  }

  return state;
}

static bool
tagbarvisible(const TagBarState *state, unsigned int tag)
{
  unsigned int mask = 1u << tag;

  if (config.tag_visibility == RAWM_TAG_VISIBILITY_ALL)
    return true;

  return (state->occupied | state->viewed) & mask;
}

static void
drawbar(Monitor *m)
{
  TagBarState  state = tagbarstate(m);
  int          x, title_right;
  unsigned int i;
  XftColor    *col;
  Client      *c;
  bool         drew_tag = false;

#ifdef SYSTRAY
  resizebarwin(m);
#endif /* SYSTRAY */

  XSetForeground(dpy, dc.gc, dc.colors[0][ColBG].pixel);
  XFillRectangle(dpy, dc.drawable, dc.gc, 0, 0, m->ww, bh);

  dc.x = (int)config.bar_padding;

  for (i = 0; i < config.tag_count; i++)
  {
    if (!tagbarvisible(&state, i))
      continue;
    if (drew_tag)
      dc.x += (int)config.bar_item_spacing;

    dc.w = TEXTW(m->tag_names[i]);

    col = dc.colors[(state.viewed & 1u << i)
                      ? SchemeSel
                      : (state.urgent & 1u << i ? SchemeUrg : SchemeNorm)];

    if ((state.viewed | state.urgent) & 1u << i)
    {
      drawtextcell(m->tag_names[i], col, true);
      drawbarindicator(m, dc.x, dc.w, col);
    }
    else
    {
      drawtext(m->tag_names[i], col, true);
      if (barhovermatches(m, ClkTagBar, 1u << i, NULL))
        drawbarindicator(m, dc.x, dc.w, dc.colors[SchemeNorm]);
    }

    dc.x += dc.w;
    drew_tag = true;
  }

  if (drew_tag)
    dc.x += (int)config.bar_section_spacing;

  /*
   * draw layout
   */

  /* tiled layout: []= X
   * where X is the number of clients in master area */
  if (m->lt[m->sellt]->arrange == tile)
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "%s %d",
             m->lt[m->sellt]->symbol, m->nmaster);

  /* float/monocle layout: [X/Y] or <X/Y>
   * where X is the current window number, and Y is the number of
   * clients in master area */
  else if (  (   m->lt[m->sellt]->arrange == monocle
              || m->lt[m->sellt]->arrange == NULL )
           && m == selmon /* update only on selected monitor */
           )
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

    snprintf(m->ltsymbol, sizeof m->ltsymbol, "%s %u/%u",
             m->lt[m->sellt]->symbol, j, i);
  }

  else if (m->lt[m->sellt]->arrange == bstack)
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "%s %d",
             m->lt[m->sellt]->symbol, m->nmaster);

  else if (m->lt[m->sellt]->arrange == bstackhoriz)
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "%s %d",
             m->lt[m->sellt]->symbol, m->nmaster);

  else if (m->lt[m->sellt]->arrange == grid)
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "%s",
             m->lt[m->sellt]->symbol);

  dc.w = blw = TEXTW(m->ltsymbol);
  {
    XftColor layout_col[ColLast];

    memcpy(layout_col, dc.colors[SchemeNorm], sizeof layout_col);
    layout_col[ColBG] = dc.colors[SchemeNorm][ColBorder];
    drawtextcell(m->ltsymbol, layout_col, true);
    if (barhovermatches(m, ClkLtSymbol, 0, NULL))
      drawbarindicator(m, dc.x, dc.w, dc.colors[SchemeNorm]);
  }
  dc.x += dc.w;
  dc.x += (int)config.bar_section_spacing;
  x = dc.x;
  title_right = m->ww - (int)config.bar_padding;

  if (m == selmon)
  {
    int status_right = m->ww - (int)config.bar_padding;
    int status_w = statuswidth();
    int status_x;

    /* status is only drawn on selected monitor */
#ifdef SYSTRAY
    if (config.show_systray  &&  m == selmon)
    {
      status_right -= getsystraywidth();
      status_right -= (int)config.bar_section_spacing;
    }
#endif /* SYSTRAY */

    status_x = status_right - status_w;
    if (status_x < x)
      status_x = x;
    if (status_w)
    {
      drawstatus(status_x, status_right);
      title_right = MAX(x, status_x - (int)config.bar_section_spacing);
    }
    else
      title_right = status_right;
  }

  if ((dc.w = title_right - x) > bh)
  {
    dc.x = x;
#ifdef WINTITLE
    if (m->sel)
    {
      XftColor title_col[ColLast];

      /* Keep the title surface neutral; use the semantic scheme only for
       * foreground authority and the workspace-facing focus marker. */
      memcpy(title_col, dc.colors[SchemeNorm], sizeof title_col);
      col = m == selmon
              ? dc.colors[clientselectedscheme(m->sel)]
              : dc.colors[SchemeNorm];
      title_col[ColFG] = col[ColFG];
      title_col[ColBorder] = col[ColBorder];

      drawtextcentered(m->sel->name, title_col, true);
      if (m == selmon)
      {
        int marker_width =
            MIN(dc.w, dc.font.ascent + dc.font.descent +
                      (barhovermatches(m, ClkWinTitle, 0, NULL) ? 24 : 12));
        int marker_x = dc.x + (dc.w - marker_width) / 2;

        drawbarindicator(m, marker_x, marker_width, col);
      }
    }
    else
#endif /* WINTITLE */
      drawtext(NULL, dc.colors[0], false);
  }

  XCopyArea(dpy, dc.drawable, m->barwin, dc.gc, 0, 0, m->ww, bh, 0, 0);
  XSync(dpy, false);
}

static void
drawbarindicator(const Monitor *m, int x, int width,
                 XftColor col[ColLast])
{
  unsigned int indicator;
  int y;

  if (!m || width <= 0)
    return;

  indicator = MIN(config.bar_indicator_size, (unsigned int)dc.h);
  if (!indicator)
    return;

  y = m->topbar ? dc.h - (int)indicator : 0;
  XSetForeground(dpy, dc.gc, col[ColBorder].pixel);
  XFillRectangle(dpy, dc.drawable, dc.gc, x, y, width, indicator);
}

static void
drawbars(void)
{
  Monitor *m;

  for (m = mons; m; m = m->next)
    drawbar(m);

#ifdef SYSTRAY
  updatesystray();
#endif /* SYSTRAY */
}

static void
drawstatus(int x, int right)
{
  size_t i;
  int cursor = x;
  int separator_width = 0;

  if (config.status_separator && config.status_separator[0])
    separator_width = (int)textnw(config.status_separator,
                                  strlen(config.status_separator));

  for (i = 0; i < statusdata.count && cursor < right; ++i)
  {
    int width;

    if (i)
    {
      cursor += (int)config.bar_item_spacing;
      if (cursor >= right)
        break;
    }

    if (i && separator_width)
    {
      width = MIN(separator_width, right - cursor);
      dc.x = cursor;
      dc.w = width;
      drawtext(config.status_separator, dc.colors[0], false);
      cursor += separator_width;
      if (cursor >= right)
        break;
    }

    width = TEXTW(statusdata.segments[i].text);
    dc.x = cursor;
    dc.w = MIN(width, right - cursor);
    {
      int scheme = statusscheme(statusdata.segments[i].state);

      if (statusdata.segments[i].state == StatusNormal)
      {
        drawtext(statusdata.segments[i].text, dc.colors[SchemeNorm], true);
        if (barhovermatches(selmon, ClkStatusText, 0,
                            statusdata.segments[i].id))
          drawbarindicator(selmon, dc.x, dc.w, dc.colors[SchemeNorm]);
      }
      else
      {
        XftColor alert_col[ColLast];

        memcpy(alert_col, dc.colors[scheme], sizeof alert_col);
        drawtextcell(statusdata.segments[i].text, alert_col, true);
        drawbarindicator(selmon, dc.x, dc.w, alert_col);
      }
    }
    cursor += width;
  }
}

static void
drawtext(const char *text, XftColor col[ColLast], bool pad)
{
  drawtextaligned(text, col, pad, false, 0);
}

static void
drawtextcell(const char *text, XftColor col[ColLast], bool pad)
{
  drawtextaligned(text, col, pad, false, config.bar_cell_margin);
}

static void
drawtextcentered(const char *text, XftColor col[ColLast], bool pad)
{
  drawtextaligned(text, col, pad, true, 0);
}

static void
drawtextaligned(const char *text, XftColor col[ColLast], bool pad,
                bool centered, unsigned int margin)
{
  char     *buf;
  int       box_h, box_y, x, y, h;
  size_t    len, olen;
  XftDraw  *d;

  margin = MIN(margin, dc.h > 1 ? (unsigned int)(dc.h - 1) / 2 : 0);
  box_y = dc.y + (int)margin;
  box_h = dc.h - (int)(2 * margin);

  XSetForeground(dpy, dc.gc, col[ColBG].pixel);
  XFillRectangle(dpy, dc.drawable, dc.gc, dc.x, box_y, dc.w, box_h);

  if (!text)
    return;

  olen = strlen(text);
  h = pad ? (dc.font.ascent + dc.font.descent) : 0;
  y = box_y + (box_h + dc.font.ascent - dc.font.descent) / 2;

  for (len = olen;
       len && textnw(text, (unsigned int)len) > (dc.w - h);
       len--)
    /* NOTHING */;

  if (!len)
    return;

  if (!(buf = malloc(len + 1)))
    die("fatal: could not malloc() %lu bytes\n", (unsigned long)(len + 1));
  memcpy(buf, text, len);
  buf[len] = '\0';

  if (len < olen)
  {
    size_t dots = MIN(len, 3u);
    size_t i;

    for (i = 0; i < dots; ++i)
      buf[len - 1 - i] = '.';
  }

  if (centered)
  {
    int text_width = (int)textnw(buf, (unsigned int)len);
    int left_pad = h / 2;

    x = dc.x + (dc.w - text_width) / 2;
    if (x < dc.x + left_pad)
      x = dc.x + left_pad;
  }
  else
    x = dc.x + (h / 2);

  d = XftDrawCreate(dpy,
                    dc.drawable,
                    DefaultVisual(dpy, screen),
                    DefaultColormap(dpy, screen)
                    );

  XftDrawStringUtf8(d,
                    &col[ColFG],
                    dc.font.xfont,
                    x,
                    y,
                    (XftChar8 *)buf,
                    (int)len
                    );

  XftDrawDestroy(d);
  free(buf);
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
expose(XEvent *e)
{
  Monitor      *m;
  XExposeEvent *ev = &e->xexpose;

  if (ev->count != 0)
    return;

  if (osd.win != None && ev->window == osd.win)
  {
    if (osd.visible && osd.pixmap != None)
      XCopyArea(dpy, osd.pixmap, osd.win, dc.gc, 0, 0,
                osd.width, osd.height, 0, 0);
    return;
  }

  if ((m = wintomon(ev->window)))
    drawbar(m);
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

#ifdef PWKL
    XkbLockGroup(dpy, XkbUseCoreKbd, c->kbdgrp);
#endif

    detachstack(c);
    attachstack(c);
    grabbuttons(c, true);
    XSetWindowBorder(dpy, c->win,
                     dc.colors[clientselectedscheme(c)][ColBorder].pixel);
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
  drawbars();
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
}

static void
focusnstack(const Arg *arg)
{
  Client *c = NULL,  *i;
  int     j;

  if (!selmon->sel  ||  (arg->i < 0))
    return;

  for (j = 1, i = selmon->clients;
       i;
       i = i->next, j++)
  {
    if (ISVISIBLE(i)  &&  (j == arg->i))
      c = i;
  }

  if (!c)
  {
    for (j = 1;  i;  i = i->next, j++)
    {
      if (ISVISIBLE(i)  &&  (j == arg->i))
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

static Atom
getatomprop(Client *c, Atom prop)
{
  int            di;
  unsigned long  dl;
  unsigned char *p = NULL;
  Atom           da, atom = None;

#ifdef SYSTRAY
  /* FIXME getatomprop should return the number of items and a pointer
   * to the stored data instead of this workaround */
  Atom req = XA_ATOM;
  if (prop == xatom[XembedInfo])
    req = xatom[XembedInfo];

  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, req,
#else
  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
#endif /* SYSTRAY */
      &da, &di, &dl, &dl, &p) == Success && p)
  {
    atom = *(Atom *)p;
#ifdef SYSTRAY
    if (da == xatom[XembedInfo] && dl == 2)
      atom = ((Atom *)p)[1];
#endif /* SYSTRAY */
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
                         &color
                         )
      )
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

#ifdef SYSTRAY
unsigned int
getsystraywidth()
{
  unsigned int w = 0;
  Client *i;

  if (config.show_systray)
  {
    for (i = systray->icons;
         i;
         w += i->w + config.systray_spacing, i = i->next)
      /* NOTHING */;
  }

  return w ? w + config.systray_spacing : 1;
}
#endif /* SYSTRAY */

static bool
validutf8(const unsigned char *text, size_t length)
{
  size_t i = 0;

  while (i < length)
  {
    unsigned char c = text[i++];

    if (c <= 0x7f)
      continue;
    if (c >= 0xc2 && c <= 0xdf)
    {
      if (i >= length || text[i] < 0x80 || text[i] > 0xbf)
        return false;
      i++;
      continue;
    }
    if (c == 0xe0)
    {
      if (i + 1 >= length || text[i] < 0xa0 || text[i] > 0xbf ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf)
        return false;
      i += 2;
      continue;
    }
    if ((c >= 0xe1 && c <= 0xec) || (c >= 0xee && c <= 0xef))
    {
      if (i + 1 >= length || text[i] < 0x80 || text[i] > 0xbf ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf)
        return false;
      i += 2;
      continue;
    }
    if (c == 0xed)
    {
      if (i + 1 >= length || text[i] < 0x80 || text[i] > 0x9f ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf)
        return false;
      i += 2;
      continue;
    }
    if (c == 0xf0)
    {
      if (i + 2 >= length || text[i] < 0x90 || text[i] > 0xbf ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
          text[i + 2] < 0x80 || text[i + 2] > 0xbf)
        return false;
      i += 3;
      continue;
    }
    if (c >= 0xf1 && c <= 0xf3)
    {
      if (i + 2 >= length || text[i] < 0x80 || text[i] > 0xbf ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
          text[i + 2] < 0x80 || text[i + 2] > 0xbf)
        return false;
      i += 3;
      continue;
    }
    if (c == 0xf4)
    {
      if (i + 2 >= length || text[i] < 0x80 || text[i] > 0x8f ||
          text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
          text[i + 2] < 0x80 || text[i + 2] > 0xbf)
        return false;
      i += 3;
      continue;
    }
    return false;
  }
  return true;
}

static void
freestatus(StatusData *status)
{
  size_t i;

  if (!status)
    return;
  for (i = 0; i < status->count; ++i)
  {
    free(status->segments[i].id);
    free(status->segments[i].text);
  }
  free(status->segments);
  status->segments = NULL;
  status->count = 0;
}

static bool
statusidvalid(const unsigned char *id, size_t length)
{
  size_t i;

  if (!id || !length || length > RAWM_STATUS_ID_MAX)
    return false;
  for (i = 0; i < length; ++i)
  {
    unsigned char c = id[i];

    if (   (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9')
        || c == '.' || c == '_' || c == '-')
      continue;
    return false;
  }
  return true;
}

static bool
statustextvalid(const unsigned char *text, size_t length)
{
  size_t i;

  if (!text || !length || !validutf8(text, length))
    return false;
  for (i = 0; i < length; ++i)
    if (text[i] < 0x20 || text[i] == 0x7f)
      return false;
  return true;
}

static bool
parsestatusstate(const unsigned char *text, size_t length, StatusState *state)
{
  if (length == 6 && !memcmp(text, "normal", 6))
    *state = StatusNormal;
  else if (length == 7 && !memcmp(text, "warning", 7))
    *state = StatusWarning;
  else if (length == 8 && !memcmp(text, "critical", 8))
    *state = StatusCritical;
  else
    return false;
  return true;
}

static bool
parsestatus(const unsigned char *value, size_t length, StatusData *status)
{
  StatusData parsed = {0};
  size_t pos = 0;

  if (!status)
    return false;
  if (!length)
  {
    *status = parsed;
    return true;
  }
  if (!value || length > RAWM_STATUS_V2_MAX || value[length - 1] != '\n')
    return false;

  while (pos < length)
  {
    const unsigned char *line = value + pos;
    const unsigned char *newline = memchr(line, '\n', length - pos);
    const unsigned char *first, *second;
    size_t line_len, id_len, state_len, text_len, i;
    StatusSegment *segments;
    StatusSegment *segment;
    StatusState state;

    if (!newline)
      goto fail;
    line_len = (size_t)(newline - line);
    first = memchr(line, '\t', line_len);
    if (!first)
      goto fail;
    second = memchr(first + 1, '\t', line_len - (size_t)(first + 1 - line));
    if (!second)
      goto fail;

    id_len = (size_t)(first - line);
    state_len = (size_t)(second - first - 1);
    text_len = line_len - (size_t)(second + 1 - line);
    if (!statusidvalid(line, id_len) ||
        !parsestatusstate(first + 1, state_len, &state) ||
        !statustextvalid(second + 1, text_len))
      goto fail;

    for (i = 0; i < parsed.count; ++i)
      if (strlen(parsed.segments[i].id) == id_len &&
          !memcmp(parsed.segments[i].id, line, id_len))
        goto fail;

    segments = realloc(parsed.segments,
                       (parsed.count + 1) * sizeof(*parsed.segments));
    if (!segments)
      die("fatal: could not realloc status segments\n");
    parsed.segments = segments;
    segment = &parsed.segments[parsed.count];
    memset(segment, 0, sizeof(*segment));

    segment->id = malloc(id_len + 1);
    segment->text = malloc(text_len + 1);
    if (!segment->id || !segment->text)
      die("fatal: could not allocate status segment\n");
    memcpy(segment->id, line, id_len);
    segment->id[id_len] = '\0';
    memcpy(segment->text, second + 1, text_len);
    segment->text[text_len] = '\0';
    segment->state = state;

    parsed.count++;
    pos += line_len + 1;
  }

  *status = parsed;
  return true;

fail:
  freestatus(&parsed);
  return false;
}

static bool
getstatusprop(StatusData *status)
{
  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *value = NULL;
  bool ok = false;
  long request_units = (long)((RAWM_STATUS_V2_MAX + 3u) / 4u + 1u);

  if (!status)
    return false;
  memset(status, 0, sizeof(*status));

  if (   XGetWindowProperty(dpy, root, statusatom, 0, request_units, False,
                            AnyPropertyType, &actual_type, &actual_format,
                            &nitems, &bytes_after, &value) != Success
      || actual_type != utf8atom
      || actual_format != 8
      || bytes_after != 0
      || nitems > RAWM_STATUS_V2_MAX
      || (nitems && memchr(value, '\0', nitems))
      )
    goto out;

  ok = parsestatus(value, (size_t)nitems, status);

out:
  if (value)
    XFree(value);
  return ok;
}

static int
statusscheme(StatusState state)
{
  switch (state)
  {
  case StatusWarning:
    return 2;
  case StatusCritical:
    return 3;
  case StatusNormal:
  default:
    return 0;
  }
}

static const char *
statusidat(int x, int start, int right)
{
  size_t i;
  int cursor = start;
  int separator_width = 0;

  if (x < start || x >= right)
    return NULL;
  if (config.status_separator && config.status_separator[0])
    separator_width = (int)textnw(config.status_separator,
                                  strlen(config.status_separator));

  for (i = 0; i < statusdata.count && cursor < right; ++i)
  {
    int width;

    if (i)
    {
      cursor += (int)config.bar_item_spacing;
      if (cursor >= right)
        break;
    }

    if (i && separator_width)
    {
      cursor += separator_width;
      if (cursor >= right)
        break;
    }

    width = TEXTW(statusdata.segments[i].text);
    if (x >= cursor && x < MIN(cursor + width, right))
      return statusdata.segments[i].id;
    cursor += width;
  }

  return NULL;
}

static int
statuswidth(void)
{
  size_t i;
  int width = 0;
  int separator_width = 0;

  if (!statusdata.count)
    return 0;
  if (config.status_separator && config.status_separator[0])
    separator_width = (int)textnw(config.status_separator,
                                  strlen(config.status_separator));
  for (i = 0; i < statusdata.count; ++i)
  {
    if (i)
      width += separator_width + (int)config.bar_item_spacing;
    width += TEXTW(statusdata.segments[i].text);
  }
  return width;
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

static void
setsurfaceproperties(Window window, const char *instance, const char *surface,
                     unsigned long monitor)
{
  XClassHint class_hint = { (char *)instance, (char *)"rawm" };

  XSetClassHint(dpy, window, &class_hint);
  XStoreName(dpy, window, instance);
  XChangeProperty(dpy, window, netatom[NetWMName], utf8atom, 8,
                  PropModeReplace, (const unsigned char *)instance,
                  (int)strlen(instance));
  XChangeProperty(dpy, window, netatom[NetWMWindowType], XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)&dockatom, 1);
  XChangeProperty(dpy, window, surfaceatom, utf8atom, 8, PropModeReplace,
                  (const unsigned char *)surface, (int)strlen(surface));
  setcardinalproperty(window, rawmatom[RawmAtomMonitor], monitor);
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
                        None
                        );
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
                  None
                  );
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
    for (unsigned int i = 0; i < key_count; i++)
    {
      if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
      {
        for (unsigned int j = 0;  j < LENGTH(modifiers);  j++)
          XGrabKey(dpy,
                   code,
                   keys[i].mod | modifiers[j],
                   root,
                   true,
                   GrabModeAsync,
                   GrabModeAsync
                   );
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
  osdshowf(selmon, "master %d", selmon->nmaster);
}

static void
initfont(const char *fontstr)
{
  if (   !(dc.font.xfont = XftFontOpenName(dpy, screen, fontstr))
      && !(dc.font.xfont = XftFontOpenName(dpy, screen, "fixed"))
      )
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
  keysym = XkbKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0, 0);

  for (i = 0; i < key_count; i++)
  {
    if (   (keysym == keys[i].keysym)
        && (CLEANMASK(keys[i].mod) == CLEANMASK(ev->state))
        &&  keys[i].func
        )
      keys[i].func( &(keys[i].arg) );
  }
}

static void
killclient(__attribute__((unused)) const Arg *arg)
{
  if (!selmon->sel)
    return;
#ifdef SYSTRAY
  if (!sendevent(selmon->sel->win, wmatom[WMDelete],
                 NoEventMask,
                 wmatom[WMDelete],
                 CurrentTime,
                 0, 0, 0))
#else
  if (!sendevent(selmon->sel,      wmatom[WMDelete]))
#endif /* SYSTRAY */
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
#ifdef PWKL
  XkbStateRec     kbd_state;
#endif /* PWKL */

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

  c->bw = config.border_width;

  wc.border_width = c->bw;

  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, dc.colors[0][ColBorder].pixel);

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

#ifdef PWKL
  XkbGetState(dpy, XkbUseCoreKbd, &kbd_state);
  c->kbdgrp = kbd_state.group;
#endif /* PWKL */
  arrange(c->mon);
  XMapWindow(dpy, c->win);
  focus(NULL);
}

static void
leavenotify(XEvent *e)
{
  XCrossingEvent *ev = &e->xcrossing;

  if (barhover.mon && ev->window == barhover.mon->barwin)
    clearbarhover();
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

#ifdef SYSTRAY
  Client *i;
  if ((i = wintosystrayicon(ev->window)))
  {
    sendevent(i->win,
              netatom[Xembed],
              StructureNotifyMask,
              CurrentTime,
              XEMBED_WINDOW_ACTIVATE,
              0,
              systray->win,
              XEMBED_EMBEDDED_VERSION
              );
    resizebarwin(selmon);
    updatesystray();
  }
#endif /* SYSTRAY */

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
  Monitor        *m;
  XMotionEvent   *ev  = &e->xmotion;

  for (m = mons; m; m = m->next)
    if (ev->window == m->barwin)
    {
      BarHit hit = barhitat(m, ev->x);
      bool interactive = barhitbound(&hit);
      unsigned int tagmask = hit.click == ClkTagBar ? hit.arg.ui : 0;
      const char *status_id =
          hit.click == ClkStatusText ? hit.status_id : NULL;
      bool changed =
          barhover.mon != (interactive ? m : NULL) ||
          barhover.click != (interactive ? hit.click : ClkRootWin) ||
          barhover.tagmask != (interactive ? tagmask : 0) ||
          ((status_id || barhover.status_id[0]) &&
           strcmp(status_id ? status_id : "", barhover.status_id));

      if (changed)
      {
        Monitor *oldmon = barhover.mon;

        barhover.mon = interactive ? m : NULL;
        barhover.click = interactive ? hit.click : ClkRootWin;
        barhover.tagmask = interactive ? tagmask : 0;
        snprintf(barhover.status_id, sizeof barhover.status_id, "%s",
                 interactive && status_id ? status_id : "");

        XDefineCursor(dpy, m->barwin,
                      interactive ? cursor[CurHand] : cursor[CurNormal]);
        if (oldmon && oldmon != m)
          drawbar(oldmon);
        drawbar(m);
      }
      return;
    }

  if (ev->window != root)
    return;

  if (  (m = recttomon(ev->x_root, ev->y_root, 1, 1))  !=  mon
      && mon
      )
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
        resize(c, nx, ny, c->w, c->h, true);

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
}

static void
nametag(__attribute__((unused)) const Arg *arg)
{
  char *p, buf[256];
  FILE *f;
  size_t i;

  snprintf(buf, sizeof(buf),
    "dmenu -p '%s' -fn '%s' -nb '%s' -nf '%s' -sb '%s' -sf '%s' "
    "</dev/null", "Current tag name: ",
    config.font,
    config.colors[0][ColBG],
    config.colors[0][ColFG],
    config.colors[1][ColBG],
    config.colors[1][ColFG]);

  errno = 0; /* popen(3p) says on failure it "may" set errno */
  if (!(f = popen(buf, "r")))
  {
    fprintf(stderr, "rawm: nametag: Could not popen '%s'", buf);
    if (errno)
      fprintf(stderr, ": %s\n", strerror(errno));
    else
      fprintf(stderr, "\n");
    return;
  }

  if (!(p = fgets(buf, MAX_TAGNAMELEN, f))  &&  ferror(f))
    fprintf(stderr, "rawm: nametag: fgets failed: %s\n",
            strerror(errno));

  if (pclose(f) < 0)
    fprintf(stderr, "rawm: nametag: pclose failed: %s\n",
            strerror(errno));

  if (!p)
    return;

  if ((p = strchr(buf, '\n')))
    *p = '\0';

  for (i = 0; i < config.tag_count; i++)
  {
    if (selmon->tagset[selmon->seltags] & (1 << i))
    {
      if (strlen(buf))
      {
        snprintf(selmon->tag_names[i], sizeof selmon->tag_names[i], "%zu/", i + 1);
        strncat(selmon->tag_names[i], buf,
                sizeof selmon->tag_names[i] - strlen(selmon->tag_names[i]) - 1);
      }
      else
        snprintf(selmon->tag_names[i], sizeof selmon->tag_names[i], "%zu", i + 1);
    }
  }

  drawbar(selmon);
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

static uint64_t
monotonicms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    die("fatal: clock_gettime failed: %s\n", strerror(errno));

  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void
osdcreate(void)
{
  XSetWindowAttributes wa;

  if (!config.show_osd || osd.win != None)
    return;

  memset(&wa, 0, sizeof wa);
  wa.override_redirect = true;
  wa.background_pixel = dc.colors[SchemeNorm][ColBG].pixel;
  wa.event_mask = ExposureMask;

  osd.win =
      XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
                    DefaultDepth(dpy, screen), CopyFromParent,
                    DefaultVisual(dpy, screen),
                    CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);

  setsurfaceproperties(osd.win, "rawm-osd", "osd", 0);
}

static void
osddraw(void)
{
  XftDraw *draw;
  XGlyphInfo extents;
  unsigned int padding;
  unsigned int text_width;
  int x, y;

  if (!osd.mon || osd.win == None || !osd.text[0])
    return;

  padding = config.osd_padding;
  XftTextExtentsUtf8(dpy, dc.font.xfont,
                     (const FcChar8 *)osd.text, (int)strlen(osd.text),
                     &extents);
  text_width = extents.xOff > 0 ? (unsigned int)extents.xOff : 1u;

  osd.width = text_width + 2u * padding;
  osd.height = (unsigned int)dc.font.height + 2u * padding;

  if (osd.width > (unsigned int)osd.mon->ww)
    osd.width = (unsigned int)osd.mon->ww;
  if (osd.height > (unsigned int)osd.mon->wh)
    osd.height = (unsigned int)osd.mon->wh;

  x = osd.mon->wx + (osd.mon->ww - (int)osd.width) / 2;
  if (config.osd_top)
    y = osd.mon->wy + (int)config.osd_offset;
  else
    y = osd.mon->wy + osd.mon->wh - (int)osd.height -
        (int)config.osd_offset;

  if (y < osd.mon->wy)
    y = osd.mon->wy;
  if (y + (int)osd.height > osd.mon->wy + osd.mon->wh)
    y = osd.mon->wy + osd.mon->wh - (int)osd.height;

  if (osd.pixmap != None)
    XFreePixmap(dpy, osd.pixmap);
  osd.pixmap = XCreatePixmap(dpy, root, osd.width, osd.height,
                             DefaultDepth(dpy, screen));

  XSetForeground(dpy, dc.gc, dc.colors[SchemeNorm][ColBG].pixel);
  XFillRectangle(dpy, osd.pixmap, dc.gc, 0, 0, osd.width, osd.height);

  draw = XftDrawCreate(dpy, osd.pixmap, DefaultVisual(dpy, screen),
                       DefaultColormap(dpy, screen));
  if (!draw)
    die("fatal: could not create Xft OSD drawing context\n");

  XftDrawStringUtf8(draw, &dc.colors[SchemeNorm][ColFG], dc.font.xfont,
                    (int)padding,
                    (int)padding + dc.font.ascent,
                    (const FcChar8 *)osd.text, (int)strlen(osd.text));
  XftDrawDestroy(draw);

  XMoveResizeWindow(dpy, osd.win, x, y, osd.width, osd.height);
  setcardinalproperty(osd.win, rawmatom[RawmAtomMonitor],
                      (unsigned long)osd.mon->num);
  XCopyArea(dpy, osd.pixmap, osd.win, dc.gc, 0, 0,
            osd.width, osd.height, 0, 0);
}

static void
osdhide(void)
{
  if (!osd.visible || osd.win == None)
    return;

  XUnmapWindow(dpy, osd.win);
  XFlush(dpy);
  osd.visible = false;
  osd.deadline_ms = 0;
}

static void
osdexpire(void)
{
  if (osd.visible && monotonicms() >= osd.deadline_ms)
    osdhide();
}

static int
osdpolltimeout(void)
{
  uint64_t now, remaining;

  if (!osd.visible)
    return -1;

  now = monotonicms();
  if (now >= osd.deadline_ms)
    return 0;

  remaining = osd.deadline_ms - now;
  return remaining > (uint64_t)INT_MAX ? INT_MAX : (int)remaining;
}

static void
osdshow(Monitor *m, const char *text)
{
  if (!running || !config.show_osd || osd.win == None ||
      !m || !text || !text[0])
    return;

  osd.mon = m;
  snprintf(osd.text, sizeof osd.text, "%s", text);
  osddraw();
  XMapRaised(dpy, osd.win);
  XFlush(dpy);
  osd.visible = true;
  osd.deadline_ms = monotonicms() + config.osd_duration_ms;
}

static void
osdshowf(Monitor *m, const char *fmt, ...)
{
  char text[sizeof osd.text];
  va_list ap;

  if (!config.show_osd)
    return;

  va_start(ap, fmt);
  vsnprintf(text, sizeof text, fmt, ap);
  va_end(ap);
  osdshow(m, text);
}

static void
osdshowview(Monitor *m)
{
  const char *tag = "all";

  if (!m)
    return;
  if (m->pertag->curtag > 0 && m->pertag->curtag <= config.tag_count)
    tag = m->tag_names[m->pertag->curtag - 1];

  osdshowf(m, "%s  ·  %s", tag, m->lt[m->sellt]->symbol);
}

static void
propertynotify(XEvent *e)
{
  Client         *c;
  Window          trans;
  XPropertyEvent *ev = &e->xproperty;

#ifdef SYSTRAY
  if ((c = wintosystrayicon(ev->window)))
  {
    if (ev->atom == XA_WM_NORMAL_HINTS)
    {
      updatesizehints(c);
      updatesystrayicongeom(c, c->w, c->h);
    }
    else
      updatesystrayiconstate(c, ev);

    resizebarwin(selmon);
    updatesystray();
  }
#endif /* SYSTRAY */

  if (   config.status_enabled
      && ev->window == root
      && ev->atom == statusatom
      )
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
      drawbars();
      if (clienturgent(c))
      {
        /* Set urgent/warning border color (from config.colors[2]) */
        XSetWindowBorder(dpy, c->win, dc.colors[2][ColFG].pixel);
      }
      break;
    default:
      break;
    }

    if (   ev->atom == XA_WM_NAME
        || ev->atom == netatom[NetWMName]
        )
    {
      updatetitle(c);
      if (c == c->mon->sel)
        drawbar(c->mon);
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

#ifdef SYSTRAY
void
removesystrayicon(Client *i)
{
  Client **ii;

  if (!config.show_systray || !i)
    return;

  for (ii = &systray->icons;  *ii && *ii != i;  ii = &(*ii)->next)
    /* NOTHING */;

  if (ii)
    *ii = i->next;

  free(i);
}
#endif /* SYSTRAY */

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

#ifdef SYSTRAY
void
resizebarwin(Monitor *m)
{
  unsigned int w = m->ww;

  if (config.show_systray  &&  m == selmon)
    w -= getsystraywidth();

  XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, w, bh);
}
#endif /* SYSTRAY */

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
}

#ifdef SYSTRAY
void
resizerequest(XEvent *e)
{
  XResizeRequestEvent *ev = &e->xresizerequest;
  Client *i;

  if ((i = wintosystrayicon(ev->window)))
  {
    updatesystrayicongeom(i, ev->width, ev->height);
    resizebarwin(selmon);
    updatesystray();
  }
}
#endif /* SYSTRAY */

static void
restack(Monitor *m)
{
  Client         *c;
  XEvent          ev;
  XWindowChanges  wc;

  drawbar(m);

  if (!m->sel)
    return;

  if (m->sel->isfloating  ||  !m->lt[m->sellt]->arrange)
    XRaiseWindow(dpy, m->sel->win);

  if (m->lt[m->sellt]->arrange)
  {
    wc.stack_mode = Below;
    wc.sibling    = m->barwin;

    for (c = m->stack;  c;  c = c->snext)
    {
      if (!c->isfloating && ISVISIBLE(c))
      {
        XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
        wc.sibling = c->win;
      }
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
  struct pollfd fds[2];

  fds[0].fd = ConnectionNumber(dpy);
  fds[0].events = POLLIN;
  fds[1].fd = signalpipe[0];
  fds[1].events = POLLIN;

  XSync(dpy, false);
  while (running)
  {
    while (running && XPending(dpy))
    {
      XNextEvent(dpy, &ev);
      if (handler[ev.type])
        handler[ev.type](&ev);
    }
    if (!running)
      break;

    osdexpire();

    {
      int poll_result = poll(fds, 2, osdpolltimeout());

      if (poll_result < 0)
      {
        if (errno == EINTR)
          continue;
        die("fatal: poll failed: %s\n", strerror(errno));
      }
      if (poll_result == 0)
      {
        osdexpire();
        continue;
      }
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

      if (   XGetTransientForHint(dpy, wins[i], &d1)
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

#ifdef SYSTRAY
static bool
sendevent(Window w, Atom proto, int mask,
    long d0, long d1, long d2, long d3, long d4)
{
  int     n;
  Atom   *protocols, mt;
  bool    exists = false;
  XEvent  ev;

  if (   proto == wmatom[WMTakeFocus]
      || proto == wmatom[WMDelete]
      )
  {
    mt = wmatom[WMProtocols];

    if (XGetWMProtocols(dpy, w, &protocols, &n))
    {
      while (!exists && n--)
        exists = protocols[n] == proto;

      XFree(protocols);
    }
  }
  else
  {
    exists = True;
    mt = proto;
  }

  if (exists)
  {
    ev.type                 = ClientMessage;
    ev.xclient.window       = w;
    ev.xclient.message_type = mt;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = d0;
    ev.xclient.data.l[1]    = d1;
    ev.xclient.data.l[2]    = d2;
    ev.xclient.data.l[3]    = d3;
    ev.xclient.data.l[4]    = d4;

    XSendEvent(dpy, w, False, mask, &ev);
  }

  return exists;
}
#else
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
#endif /* SYSTRAY */

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

#ifdef SYSTRAY
  sendevent(c->win,
            wmatom[WMTakeFocus],
            NoEventMask,
            wmatom[WMTakeFocus],
            CurrentTime,
            0, 0, 0);
#else
  sendevent(c, wmatom[WMTakeFocus]);
#endif /* SYSTRAY */
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

  strncpy(selmon->ltsymbol,
          selmon->lt[selmon->sellt]->symbol,
          sizeof selmon->ltsymbol
          );

  if (selmon->sel)
    arrange(selmon);
  else
    drawbar(selmon);

  osdshowf(selmon, "%s", selmon->lt[selmon->sellt]->symbol);
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
  osdshowf(selmon, "master %d%%", (int)(selmon->mfact * 100.0f + 0.5f));
}

static void
setup(void)
{
  XSetWindowAttributes wa;

  if (pipe(signalpipe) < 0 ||
      setsignalpipeflags(signalpipe[0]) < 0 ||
      setsignalpipeflags(signalpipe[1]) < 0)
    die("fatal: could not create signal pipe: %s\n", strerror(errno));

  /* Clean up any inherited zombies before entering the event loop. */
  while (waitpid(-1, NULL, WNOHANG) > 0)
    /* NOTHING */;

  if (installsignalhandler(SIGCHLD, sigchld) < 0 ||
      installsignalhandler(SIGHUP, sighup) < 0 ||
      installsignalhandler(SIGTERM, sigterm) < 0 ||
      installsignalhandler(SIGINT, sigterm) < 0)
    die("fatal: could not install signal handler: %s\n", strerror(errno));

  /* init screen */
  screen  = DefaultScreen(dpy);
  root    = RootWindow(dpy, screen);

  initfont(config.font);

  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  bh = dc.h = config.bar_height ? config.bar_height : dc.font.height + 2;

  updategeom();

  /* init atoms */
  wmatom[WMProtocols]               = XInternAtom(dpy, "WM_PROTOCOLS",                 false);
  wmatom[WMDelete]                  = XInternAtom(dpy, "WM_DELETE_WINDOW",             false);
  wmatom[WMState]                   = XInternAtom(dpy, "WM_STATE",                     false);
  wmatom[WMTakeFocus]               = XInternAtom(dpy, "WM_TAKE_FOCUS",                false);

  netatom[NetActiveWindow]          = XInternAtom(dpy, "_NET_ACTIVE_WINDOW",           false);
  netatom[NetSupported]             = XInternAtom(dpy, "_NET_SUPPORTED",               false);
#ifdef SYSTRAY
  netatom[NetSystemTray]            = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0",          false);
  netatom[NetSystemTrayOP]          = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE",      false);
  netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", false);
#endif /* SYSTRAY */
  netatom[NetWMName]                = XInternAtom(dpy, "_NET_WM_NAME",                 false);
  netatom[NetWMState]               = XInternAtom(dpy, "_NET_WM_STATE",                false);
  netatom[NetClientList]            = XInternAtom(dpy, "_NET_CLIENT_LIST",             false);
  netatom[NetWMFullscreen]          = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN",     false);
  netatom[NetWMWindowType]          = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE",          false);
  netatom[NetWMWindowTypeDialog]    = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG",   false);
  utf8atom                           = XInternAtom(dpy, "UTF8_STRING",                   false);
  dockatom                           = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK",      false);
  statusatom                         = XInternAtom(dpy, "_RAWM_STATUS_V2",               false);
  surfaceatom                        = XInternAtom(dpy, "_RAWM_SURFACE",                 false);
  rawmatom[RawmAtomFloating]         = XInternAtom(dpy, "_RAWM_FLOATING",                false);
  rawmatom[RawmAtomSelected]         = XInternAtom(dpy, "_RAWM_SELECTED",                false);
  rawmatom[RawmAtomLayout]           = XInternAtom(dpy, "_RAWM_LAYOUT",                  false);
  rawmatom[RawmAtomMonitor]          = XInternAtom(dpy, "_RAWM_MONITOR",                 false);
#ifdef SYSTRAY
  xatom[Manager]                    = XInternAtom(dpy, "MANAGER",                      false);
  xatom[Xembed]                     = XInternAtom(dpy, "_XEMBED",                      false);
  xatom[XembedInfo]                 = XInternAtom(dpy, "_XEMBED_INFO",                 false);
#endif /* SYSTRAY */

  /* init cursors */
  cursor[CurNormal]  = XCreateFontCursor(dpy, XC_left_ptr);
  cursor[CurHand]    = XCreateFontCursor(dpy, XC_hand2);
  cursor[CurResize]  = XCreateFontCursor(dpy, XC_sizing);
  cursor[CurMove]    = XCreateFontCursor(dpy, XC_fleur);

  /* init appearance */
  for (int i = 0; i < RAWM_COLOR_SCHEMES; i++)
  {
    dc.colors[i][ColBorder] = getcolor(config.colors[i][ColBorder]);
    dc.colors[i][ColFG]     = getcolor(config.colors[i][ColFG]);
    dc.colors[i][ColBG]     = getcolor(config.colors[i][ColBG]);
  }

  dc.drawable        = XCreatePixmap(dpy,
                                     root,
                                     DisplayWidth(dpy, screen),
                                     bh,
                                     DefaultDepth(dpy, screen)
                                     );
  dc.gc              = XCreateGC(dpy, root, 0, NULL);

  XSetLineAttributes(dpy, dc.gc, 1, LineSolid, CapButt, JoinMiter);

  osdcreate();

#ifdef SYSTRAY
  /* init system tray */
  updatesystray();
#endif /* SYSTRAY */

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
                  NetLast
                  );

  XDeleteProperty(dpy, root, netatom[NetClientList]);

  /* select for events */
  wa.cursor     = cursor[CurNormal];
  wa.event_mask =   SubstructureRedirectMask
                  | SubstructureNotifyMask
                  | ButtonPressMask
                  | PointerMotionMask
                  | EnterWindowMask
                  | LeaveWindowMask
                  | StructureNotifyMask
                  | PropertyChangeMask;

  XChangeWindowAttributes(dpy,
                          root,
                          CWEventMask | CWCursor,
                          &wa
                          );
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
  ssize_t n;
  bool reap = false;

  while ((n = read(signalpipe[0], bytes, sizeof(bytes))) > 0)
  {
    ssize_t i;
    for (i = 0; i < n; ++i)
      if (bytes[i] == (unsigned char)SIGCHLD)
        reap = true;
  }
  if (reap)
    while (waitpid(-1, NULL, WNOHANG) > 0)
      /* NOTHING */;
}

static void
sigchld(int sig)
{
  wakesignalpipe(sig);
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
spawn(const Arg *arg)
{
  if (fork() == 0)
  {
    if (dpy)
      close(ConnectionNumber(dpy));

    setsid();
    execvp(((char **)arg->v)[0],
            (char **)arg->v);
    fprintf(stderr, "rawm: execvp %s", ((char **)arg->v)[0]);
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
  if (!selmon->sel  ||  !mons->next)
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
togglebar(__attribute__((unused)) const Arg *arg)
{
  selmon->showbar =
    selmon->pertag->showbars[selmon->pertag->curtag] =
      !selmon->showbar;

  updatebarpos(selmon);

#ifdef SYSTRAY
  resizebarwin(selmon);

  if (config.show_systray)
  {
    XWindowChanges wc;

    if (!selmon->showbar)
      wc.y = -bh;
    else if (selmon->showbar)
    {
      wc.y = 0;

      if (!selmon->topbar)
        wc.y = selmon->mh - bh;
    }

    XConfigureWindow(dpy, systray->win, CWY, &wc);
  }
#else
  XMoveResizeWindow(dpy,
                    selmon->barwin,
                    selmon->wx,
                    selmon->by,
                    selmon->ww,
                    bh
                    );
#endif /* SYSTRAY */

  arrange(selmon);
}

static void
togglefloating(__attribute__((unused)) const Arg *arg)
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
  osdshowf(c->mon, "%s", clientiselevated(c) ? "float" : "tile");
}

static void
togglefullscr(__attribute__((unused)) const Arg *arg)
{
  if (selmon->sel)
  {
    Client *c = selmon->sel;

    setfullscreen(c, !c->isfullscreen);
    osdshowf(c->mon, "%s", c->isfullscreen ? "fullscreen" : "windowed");
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
      != selmon->pertag->showbars[selmon->pertag->curtag]
      )
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
#endif /* PWKL */

  grabbuttons(c, false);
  XSetWindowBorder(dpy, c->win, dc.colors[0][ColBorder].pixel);
  setcardinalproperty(c->win, rawmatom[RawmAtomSelected], 0);

  if (setfocus)
  {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }

#ifdef PWKL
  XkbGetState(dpy, XkbUseCoreKbd, &kbd_state);
  c->kbdgrp = kbd_state.group;
#endif /* PWKL */
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

  if ((c = wintoclient(ev->window)))
  {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, false);
  }
#ifdef SYSTRAY
  else if ((c = wintosystrayicon(ev->window)))
  {
    removesystrayicon(c);
    resizebarwin(selmon);
    updatesystray();
  }
#endif /* SYSTRAY */
}

static void
updatebars(void)
{
#ifdef SYSTRAY
  unsigned int w;
#endif /* SYSTRAY */
  Monitor *m;

  XSetWindowAttributes wa = {
    .override_redirect  = true,
    .background_pixmap  = ParentRelative,
    .event_mask         = ButtonPressMask | ExposureMask |
                          PointerMotionMask | LeaveWindowMask
  };

  for (m = mons; m; m = m->next)
  {
#ifdef SYSTRAY
    w = m->ww;
    if (config.show_systray && m == selmon)
      w -= getsystraywidth();

    m->barwin =
      XCreateWindow(dpy, root, m->wx, m->by, w,     bh, 0,
                    DefaultDepth(dpy, screen),
#else
    m->barwin =
      XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0,
                    DefaultDepth(dpy, screen),
#endif /* SYSTRAY */
                    CopyFromParent,
                    DefaultVisual(dpy, screen),
                    CWOverrideRedirect | CWBackPixmap | CWEventMask,
                    &wa
                    );

    setsurfaceproperties(m->barwin, "rawm-bar", "bar",
                         (unsigned long)m->num);
    XDefineCursor(dpy, m->barwin, cursor[CurNormal]);
    XMapRaised(dpy, m->barwin);

    if (config.opacity >= 0  &&  config.opacity <= 1)
    {
      XChangeProperty(dpy,
                      m->barwin,
                      XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", false),
                      XA_CARDINAL,
                      32,
                      PropModeReplace,
                      (unsigned char *) &opacity,
                      1L
                      );
    }
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
    m->by  = m->topbar  ?   m->wy        :  (m->wy + m->wh);
    m->wy  = m->topbar  ?  (m->wy + bh)  :   m->wy;
  }
  else
    m->by  = -bh;
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

          updatebarpos(m);
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

#ifdef SYSTRAY
void
updatesystrayicongeom(Client *i, int w, int h)
{
  if (!i)
    return;

  i->h = bh;

  if      (w == h)
    i->w = bh;
  else if (h == bh)
    i->w = w;
  else
    i->w = (int)((float)bh * ((float)w / (float)h));

  applysizehints(i, &(i->x), &(i->y), &(i->w), &(i->h), False);

  /* force icons into the systray dimenons if they don't want to */
  if (i->h > bh)
  {
    if (i->w == i->h)
      i->w = bh;
    else
      i->w = (int)((float)bh * ((float)i->w / (float)i->h));

    i->h = bh;
  }
}

void
updatesystrayiconstate(Client *i, XPropertyEvent *ev)
{
  long flags;
  int code = 0;

  if (   !config.show_systray
      || !i
      || ev->atom != xatom[XembedInfo]
      || !(flags = getatomprop(i, xatom[XembedInfo]))
      )
    return;

  if (flags & XEMBED_MAPPED && !i->tags)
  {
    i->tags = 1;
    code = XEMBED_WINDOW_ACTIVATE;
    XMapRaised(dpy, i->win);
    setclientstate(i, NormalState);
  }
  else if (!(flags & XEMBED_MAPPED) && i->tags)
  {
    i->tags = 0;
    code = XEMBED_WINDOW_DEACTIVATE;
    XUnmapWindow(dpy, i->win);
    setclientstate(i, WithdrawnState);
  }
  else
    return;

  sendevent(i->win,
            xatom[Xembed],
            StructureNotifyMask,
            CurrentTime,
            code,
            0,
            systray->win,
            XEMBED_EMBEDDED_VERSION
            );
}

void
updatesystray(void)
{
  XSetWindowAttributes wa;
  Client *i;
  unsigned int x = selmon->mx + selmon->mw;
  unsigned int w = 1;

  if (!config.show_systray)
    return;

  if (!systray)
  {
    /* init systray */
    if (!(systray = (Systray *)calloc(1, sizeof(Systray))))
      die("fatal: could not malloc() %u bytes\n", sizeof(Systray));

    systray->win = XCreateSimpleWindow(dpy,
                                       root,
                                       x,
                                       selmon->by,
                                       w,
                                       bh,
                                       0,
                                       0,
                                       dc.colors[1][ColBG].pixel
                                       );
    setsurfaceproperties(systray->win, "rawm-systray", "systray",
                         (unsigned long)selmon->num);

    if (config.opacity >= 0  &&  config.opacity <= 1)
    {
      XChangeProperty(dpy,
                      systray->win,
                      XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False),
                      XA_CARDINAL,
                      32,
                      PropModeReplace,
                      (unsigned char *) &opacity,
                      1L
                      );
    }

    wa.event_mask        = ButtonPressMask | ExposureMask;
    wa.override_redirect = True;
    wa.background_pixmap = ParentRelative;
    wa.background_pixel  = dc.colors[0][ColBG].pixel;

    XSelectInput(dpy, systray->win, SubstructureNotifyMask);

    XChangeProperty(dpy,
                    systray->win,
                    netatom[NetSystemTrayOrientation],
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (unsigned char *)&systrayorientation,
                    1
                    );

    XChangeWindowAttributes(dpy,
                            systray->win,
                              CWEventMask
                            | CWOverrideRedirect
                            | CWBackPixel
                            | CWBackPixmap,
                            &wa
                            );

    XMapRaised(dpy, systray->win);

    XSetSelectionOwner(dpy,
                       netatom[NetSystemTray],
                       systray->win,
                       CurrentTime
                       );

    if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win)
    {
      sendevent(root,
                xatom[Manager],
                StructureNotifyMask,
                CurrentTime,
                netatom[NetSystemTray],
                systray->win,
                0,
                0
                );
      XSync(dpy, False);
    }
    else
    {
      fprintf(stderr, "rawm: unable to obtain system tray.\n");
      free(systray);
      systray = NULL;
      return;
    }
  } /* if (!systray) */

  for (w = 0, i = systray->icons; i; i = i->next)
  {
    XMapRaised(dpy, i->win);
    w += config.systray_spacing;
    XMoveResizeWindow(dpy, i->win, (i->x = w), 0, i->w, i->h);
    w += i->w;
    if (i->mon != selmon)
      i->mon = selmon;
  }

  w = w ? w + config.systray_spacing : 1;
  x -= w;
  XMoveResizeWindow(dpy, systray->win, x, selmon->by, w, bh);
  setcardinalproperty(systray->win, rawmatom[RawmAtomMonitor],
                      (unsigned long)selmon->num);

  /* redraw background */
  XSetForeground(dpy, dc.gc, dc.colors[0][ColBG].pixel);
  XFillRectangle(dpy, systray->win, dc.gc, 0, 0, w, bh);
  XSync(dpy, False);
}
#endif /* SYSTRAY */

static void
updatetitle(Client *c)
{
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);

  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

static void
updatestatus(void)
{
  StatusData next = {0};

  if (config.status_enabled)
    (void)getstatusprop(&next);
  freestatus(&statusdata);
  statusdata = next;

  drawbar(selmon);
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
  if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
    return;

  selmon->seltags ^= 1; /* toggle sel tagset */

  if (arg->ui & TAGMASK)
  {
    selmon->pertag->prevtag         = selmon->pertag->curtag;
    selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;

    if (arg->ui == ~0u)
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
      != selmon->pertag->showbars[selmon->pertag->curtag]
      )
    togglebar(NULL);

  focus(NULL);
  arrange(selmon);
  osdshowview(selmon);
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

  for (m = mons;  m;  m = m->next)
  {
    if (w == m->barwin)
      return m;
  }

  if ((c = wintoclient(w)))
    return c->mon;

  return selmon;
}

#ifdef SYSTRAY
Client *
wintosystrayicon(Window w)
{
  Client *i = NULL;

  if (!config.show_systray || !w)
    return i;

  for (i = systray->icons;
       i  &&  i->win != w;
       i = i->next
       )
    /* NOTHING */;

  return i;
}
#endif /* SYSTRAY */

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
         && win_p != win_r
         )
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
  run();
  if (restart)
    execvp(argv[0], argv);
  cleanup();
  XCloseDisplay(dpy);
  freeruntimeconfig();
  rawm_config_destroy(&config);

  return EXIT_SUCCESS;
}

/* vim: sw=2 ts=2 sts=2 et cc=72 tw=70
 * End of file. */
