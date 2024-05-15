/* dwm customization.
 * See LICENSE file for copyright and license details. */

/*********************************************************************
 * Appearance.
 */

/* Font.
 * See http://freedesktop.org/software/fontconfig/fontconfig-user.html
 */
static const char          font[]             = "Sans Mono:size=9";

/* Colors.
 */
static const char          normbordercolor[]  = "#444444";  /* window borders color */
static const char          normbgcolor[]      = "#222222";  /* background color */
static const char          normfgcolor[]      = "#bbbbbb";  /* text color in status bar */
static const char          selbordercolor[]   = "#005577";  /* active window border color */
static const char          selbgcolor[]       = "#005577";  /* window title/tags background color */
static const char          selfgcolor[]       = "#eeeeee";  /* window title/tags foreground color */

/* Other settings.
 */
static const unsigned int  borderpx           = 2;          /* border pixel of windows */
static const unsigned int  snap               = 32;         /* snap pixel */
static const bool          showbar            = true;       /* false means no bar */
static const bool          topbar             = false;      /* false means bottom bar */
static const double        defaultopacity     = 0.80;       /* transparency */

#ifdef SYSTRAY
static const unsigned int  systrayspacing     = 2;          /* systray spacing */
static const bool          showsystray        = true;       /* false means no systray */
#endif /* SYSTRAY */

/*********************************************************************
 * Layouts.
 */

static const float         mfact              = 0.55;       /* factor of master area size [0.05..0.95] */
static const int           nmaster            = 1;          /* number of clients in master area */
static const bool          resizehints        = false;      /* true means respect size hints in tiled resizals */

static const Layout layouts[] = {
/* Symbol       Arrange function */
 { "[]=",       tile            }, /* idx:0  key:Mod+t */   /* first entry is default */
 { "<1/1>",     NULL            }, /* idx:1  key:Mod+f */   /* no layout function means floating behaviour */
 { "[1/1]",     monocle         }, /* idx:2  key:Mod+m */
 { "TTT",       bstack          }, /* idx:3  key:Mod+s */
 { "===",       bstackhoriz     }, /* idx:4  key:Mod+h */
 { "###",       gaplessgrid     }, /* idx:5  key:Mod+g */
};

/*********************************************************************
 * Tags.
 */

/* Number of tags per monitor. */
#define TAGS    9

/* Max tag length is 22 (excludes tag number with semicolon).
 * If you want to change it, look at struct definition in dwm.c. */
static CustomTagLayout tags[][TAGS] = {
  /* Monitor 0.
   * Tag name  Layout idx (see "layouts" above) */
  {{"1",       2}, /* monocle */
   {"2",       0},
   {"3",       5}, /* gaplessgrid */
   {"4",       0},
   {"5",       0},
   {"6",       0},
   {"7",       0},
   {"8",       0},
   {"9",       2}, /* monocle */
   /* Don't exceed "Number of tags per monitor" or change TAGS
    * definition value (declared above) if you want more tags. */
  }
  /* Monitor 1. */
  /* ... */
};

static const Rule rules[] = {
  /*
   * Fixed Monitor.
   */

  /* class        instance  title  role  tag mask  isfloating  iscentered  monitor */
  { "Firefox",    NULL,     NULL,  NULL, 0,        false,      false,      0  },
  { "Navigator",  NULL,     NULL,  NULL, 0,        false,      false,      0  },
  /* ... */

  /*
   * Current active monitor.
   */

  /* class        instance  title  role  tag mask  isfloating  iscentered  monitor */
  { "Ktsuss",     NULL,     NULL,  NULL, 0,        true,       true,       -1 },
  { "pinentry-gtk-2", NULL, NULL,  NULL, 0,        true,       true,       -1 },
  /* ... */
};

/*********************************************************************
 * Command definitions.
 */

/* Helper for spawning shell commands in the pre dwm-5.0 fashion. */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

static const char *drun_cmd[] = {
  "dmenu_run",
    "-p", "Run:",
    "-fn", font,
    "-nb", normbgcolor,
    "-nf", normfgcolor,
    "-sb", selbgcolor,
    "-sf", selfgcolor,
    NULL,
};
static const char *pass_cmd[] = {
  "passmenu",
    "-p", "Password:",
    "-fn", font,
    "-nb", normbgcolor,
    "-nf", normfgcolor,
    "-sb", selbgcolor,
    "-sf", selfgcolor,
    NULL
};
static const char *term_cmd[] = { "st", NULL };

/* Mixer settings. */
#ifdef __DragonFly__
static const char *mixer_mute_cmd[] = { "mixer", "vol", "0",   NULL };
static const char *mixer_dec_cmd[]  = { "mixer", "vol", "-10", NULL };
static const char *mixer_inc_cmd[]  = { "mixer", "vol", "+10", NULL };
#else
static const char *mixer_mute_cmd[] = { "amixer", "sset", "Master", "toggle", NULL };
static const char *mixer_dec_cmd[]  = { "amixer", "sset", "Master", "1-",     NULL };
static const char *mixer_inc_cmd[]  = { "amixer", "sset", "Master", "1+",     NULL };
#endif /* __DragonFly__ */

/* Notebook's backlight settings. */
static const char *backlight_inc_cmd[] = { "xbacklight", "-inc", "10", NULL };
static const char *backlight_dec_cmd[] = { "xbacklight", "-dec", "10", NULL };

/*********************************************************************
 * Key definitions.
 */

/* Mod4Mask - Windows key, Mod1Mask - Alt. */
#define MODKEY Mod4Mask

/* Multimedia keyboard definitions. */
#include <X11/XF86keysym.h>

static Key keys[] = {
/* Modifier                     Key                       Function        Argument */
 { MODKEY,                      XK_r,                     spawn,          {.v = drun_cmd}         },
 { MODKEY,                      XK_p,                     spawn,          {.v = pass_cmd}         },

 { MODKEY|ShiftMask,            XK_Return,                spawn,          {.v = term_cmd}         },

 { MODKEY,                      XK_b,                     togglebar,      {0}                     },

 { MODKEY,                      XK_n,                     nametag,        {0}                     },

 { MODKEY,                      XK_j,                     focusstack,     {.i = +1}               },
 { MODKEY,                      XK_k,                     focusstack,     {.i = -1}               },

 { MODKEY,                      XK_F1,                    focusnstack,    {.i = 1}                },
 { MODKEY,                      XK_F2,                    focusnstack,    {.i = 2}                },
 { MODKEY,                      XK_F3,                    focusnstack,    {.i = 3}                },
 { MODKEY,                      XK_F4,                    focusnstack,    {.i = 4}                },
 { MODKEY,                      XK_F5,                    focusnstack,    {.i = 5}                },
 { MODKEY,                      XK_F6,                    focusnstack,    {.i = 6}                },
 { MODKEY,                      XK_F7,                    focusnstack,    {.i = 7}                },
 { MODKEY,                      XK_F8,                    focusnstack,    {.i = 8}                },
 { MODKEY,                      XK_F9,                    focusnstack,    {.i = 9}                },

 { MODKEY,                      XK_i,                     incnmaster,     {.i = +1}               },
 { MODKEY,                      XK_d,                     incnmaster,     {.i = -1}               },
 { MODKEY|ShiftMask,            XK_h,                     setmfact,       {.f = -0.01}            },
 { MODKEY|ShiftMask,            XK_l,                     setmfact,       {.f = +0.01}            },

 { MODKEY,                      XK_Return,                zoom,           {0}                     },
 { MODKEY,                      XK_Tab,                   view,           {0}                     },
 { MODKEY,                      XK_c,                     killclient,     {0}                     },
 { MODKEY,                      XK_t,                     setlayout,      {.v = &layouts[0]}      },
 { MODKEY,                      XK_f,                     setlayout,      {.v = &layouts[1]}      },
 { MODKEY,                      XK_m,                     setlayout,      {.v = &layouts[2]}      },
 { MODKEY,                      XK_s,                     setlayout,      {.v = &layouts[3]}      },
 { MODKEY,                      XK_h,                     setlayout,      {.v = &layouts[4]}      },
 { MODKEY,                      XK_g,                     setlayout,      {.v = &layouts[5]}      },
 { MODKEY,                      XK_space,                 setlayout,      {0}                     },
 { MODKEY|ShiftMask,            XK_space,                 togglefloating, {0}                     },
 { MODKEY,                      XK_0,                     view,           {.ui = ~0}              },
 { MODKEY|ShiftMask,            XK_0,                     tag,            {.ui = ~0}              },
 { MODKEY,                      XK_o,                     winview,        {0},                    },
 { MODKEY|ShiftMask,            XK_f,                     togglefullscr,  {0}                     },
 { MODKEY,                      XK_comma,                 focusmon,       {.i = -1}               },
 { MODKEY,                      XK_period,                focusmon,       {.i = +1}               },
 { MODKEY|ShiftMask,            XK_comma,                 tagmon,         {.i = -1}               },
 { MODKEY|ShiftMask,            XK_period,                tagmon,         {.i = +1}               },

 { MODKEY|ShiftMask,            XK_q,                     quit,           {0}                     },
 { MODKEY|ControlMask|ShiftMask,XK_q,                     quit,           {1}                     },

#define TAGKEYS(KEY,TAG) \
 { MODKEY,                      KEY,                      view,           {.ui = 1 << TAG}        }, \
 { MODKEY|ControlMask,          KEY,                      toggleview,     {.ui = 1 << TAG}        }, \
 { MODKEY|ShiftMask,            KEY,                      tag,            {.ui = 1 << TAG}        }, \
 { MODKEY|ControlMask|ShiftMask,KEY,                      toggletag,      {.ui = 1 << TAG}        },

        TAGKEYS(                XK_1,                                                   0)
        TAGKEYS(                XK_2,                                                   1)
        TAGKEYS(                XK_3,                                                   2)
        TAGKEYS(                XK_4,                                                   3)
        TAGKEYS(                XK_5,                                                   4)
        TAGKEYS(                XK_6,                                                   5)
        TAGKEYS(                XK_7,                                                   6)
        TAGKEYS(                XK_8,                                                   7)
        TAGKEYS(                XK_9,                                                   8)

/*
 * Multimedia keyboard shortcuts.
 */
/* Mixer */
 { 0,                           XF86XK_AudioMute,         spawn,          {.v = mixer_mute_cmd}   },
 { 0,                           XF86XK_AudioLowerVolume,  spawn,          {.v = mixer_dec_cmd}    },
 { 0,                           XF86XK_AudioRaiseVolume,  spawn,          {.v = mixer_inc_cmd}    },
/* Brightness */
 { 0,                           XF86XK_MonBrightnessDown, spawn,          {.v = backlight_dec_cmd}},
 { 0,                           XF86XK_MonBrightnessUp,   spawn,          {.v = backlight_inc_cmd}},
};

/*
 * Button definitions.
 */

/* click can be ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static Button buttons[] = {
/* Click                Event mask      Button            Function        Argument */
 { ClkLtSymbol,         0,              Button1,          setlayout,      {0}                     },
 { ClkLtSymbol,         0,              Button3,          setlayout,      {.v = &layouts[2]}      },

#ifdef WINTITLE
 { ClkWinTitle,         0,              Button2,          zoom,           {0}                     },
#endif /* WINTITLE */

 { ClkStatusText,       0,              Button2,          spawn,          {.v = term_cmd}         },
 { ClkClientWin,        MODKEY,         Button1,          movemouse,      {0}                     },
 { ClkClientWin,        MODKEY,         Button2,          togglefloating, {0}                     },
 { ClkClientWin,        MODKEY,         Button3,          resizemouse,    {0}                     },
 { ClkTagBar,           0,              Button1,          view,           {0}                     },
 { ClkTagBar,           0,              Button3,          toggleview,     {0}                     },
 { ClkTagBar,           MODKEY,         Button1,          tag,            {0}                     },
 { ClkTagBar,           MODKEY,         Button3,          toggletag,      {0}                     },
};

/* vim: sw=2 ts=2 sts=2 et cc=120
 * End of file. */
