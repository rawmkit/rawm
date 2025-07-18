# project metadata
NAME          = rawm
VERSION       = 6.0

# optional xinerama support
XINERAMA      = -DXINERAMA
XINERAMALIBS  = -lXinerama

# optional systray
SYSTRAY       = -DSYSTRAY

# optional per window keyboard layout support
PWKL          = -DPWKL

# optional windows title support
WINTITLE      = -DWINTITLE

# paths
PREFIX        = /usr/local
MANPREFIX     = ${PREFIX}/share/man

# DragonFlyBSD, FreeBSD
#X11INC       = /usr/local/include
#X11LIB       = /usr/local/lib
#FT2INC       = /usr/local/include/freetype2

# NetBSD, OpenBSD
#X11INC       = /usr/X11R6/include
#X11LIB       = /usr/X11R6/lib

# Linux
X11INC        = /usr/include
X11LIB        = /usr/lib
FT2INC        = /usr/include/freetype2

FT2LIB        = -lfontconfig -lXft

# includes and libs
INCS          = -I${X11INC} -I${FT2INC}
LIBS          = -L${X11LIB} -lX11 ${FT2LIB} ${XINERAMALIBS}

# flags
CPPFLAGS      = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L \
                -DVERSION=\"${VERSION}\" \
                ${XINERAMA} ${SYSTRAY} ${PWKL} ${WINTITLE}
CFLAGS        = -pedantic -Wall -Wextra -Wformat ${INCS} ${CPPFLAGS}
LDFLAGS       = ${LIBS}
