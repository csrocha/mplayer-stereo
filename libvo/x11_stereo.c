/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <limits.h>

#include "config.h"
#include "mp_msg.h"
#include "mp_fifo.h"
#include "libavutil/common.h"
#include "x11_stereo.h"

#ifdef X11_FULLSCREEN

#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "video_out.h"
#include "aspect.h"
#include "geometry.h"
#include "help_mp.h"
#include "osdep/timer.h"

#include <X11/Xmd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#ifdef CONFIG_XSS
#include <X11/extensions/scrnsaver.h>
#endif

#ifdef CONFIG_XDPMS
#include <X11/extensions/dpms.h>
#endif

#ifdef CONFIG_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#ifdef CONFIG_XF86VM
#include <X11/extensions/xf86vmode.h>
#endif

#ifdef CONFIG_XF86XK
#include <X11/XF86keysym.h>
#endif

#ifdef CONFIG_XV
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include "subopt-helper.h"
#endif

#include "input/input.h"
#include "input/mouse.h"

#ifdef CONFIG_GUI
#include "gui/interface.h"
#include "mplayer.h"
#endif

#define WIN_LAYER_ONBOTTOM               2
#define WIN_LAYER_NORMAL                 4
#define WIN_LAYER_ONTOP                  6
#define WIN_LAYER_ABOVE_DOCK             10

#ifndef CONFIG_X11
int fs_layer = WIN_LAYER_ABOVE_DOCK;
#else
extern int fs_layer;
#endif
static int orig_layer = 0;
static int old_gravity = NorthWestGravity;

#ifndef CONFIG_X11
int stop_xscreensaver = 0;
#endif

static int dpms_disabled = 0;

tScreenOutput ScreenLeft;
tScreenOutput ScreenRight;
int uniqueDisplay = 0;

/* output window id */
#ifndef CONFIG_X11
int vo_mouse_autohide = 0;
int vo_wm_type = 0;
int vo_fs_type = 0; // needs to be accessible for GUI X11 code
#endif
static int vo_fs_flip = 0;
char **vo_fstype_list;

/* 1 means that the WM is metacity (broken as hell) */
#ifndef CONFIG_X11
int metacity_hack = 0;
#endif

static Atom XA_NET_SUPPORTED;
static Atom XA_NET_WM_STATE;
static Atom XA_NET_WM_STATE_FULLSCREEN;
static Atom XA_NET_WM_STATE_ABOVE;
static Atom XA_NET_WM_STATE_STAYS_ON_TOP;
static Atom XA_NET_WM_STATE_BELOW;
static Atom XA_NET_WM_PID;
static Atom XA_WIN_PROTOCOLS;
static Atom XA_WIN_LAYER;
static Atom XA_WIN_HINTS;
static Atom XAWM_PROTOCOLS;
static Atom XAWM_DELETE_WINDOW;

#define XA_INIT(x) XA##x = XInternAtom(Screen->display, #x, False)

#ifdef CONFIG_XF86VM
XF86VidModeModeInfo **vidmodes_s = NULL;
XF86VidModeModeLine modeline;
#endif

static int vo_x11s_get_fs_type(int supported);

/*
 * Sends the EWMH fullscreen state event.
 *
 * action: could be one of _NET_WM_STATE_REMOVE -- remove state
 *                         _NET_WM_STATE_ADD    -- add state
 *                         _NET_WM_STATE_TOGGLE -- toggle
 */
void vo_x11s_ewmh_fullscreen_S(tScreenOutput *Screen,int action)
{
    assert(action == _NET_WM_STATE_REMOVE ||
           action == _NET_WM_STATE_ADD || action == _NET_WM_STATE_TOGGLE);

    if (vo_fs_type & vo_wm_FULLSCREEN)
    {
        XEvent xev;

        /* init X event structure for _NET_WM_FULLSCREEN client message */
        xev.xclient.type = ClientMessage;
        xev.xclient.serial = 0;
        xev.xclient.send_event = True;
        xev.xclient.message_type = XA_NET_WM_STATE;
        xev.xclient.window = Screen->window;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = action;
        xev.xclient.data.l[1] = XA_NET_WM_STATE_FULLSCREEN;
        xev.xclient.data.l[2] = 0;
        xev.xclient.data.l[3] = 0;
        xev.xclient.data.l[4] = 0;

        /* finally send that damn thing */
        if (!XSendEvent(Screen->display, DefaultRootWindow(Screen->display), False,
                        SubstructureRedirectMask | SubstructureNotifyMask,
                        &xev))
        {
            mp_msg(MSGT_VO, MSGL_ERR, MSGTR_EwmhFullscreenStateFailed);
        }
    }
}

void vo_hidecursor_S ( tScreenOutput *Screen )
{
    Cursor no_ptr;
    Pixmap bm_no;
    XColor black, dummy;
    Colormap colormap;
    static char bm_no_data[] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    if (WinID == 0)
        return;                 // do not hide if playing on the root Screen->windowdow

    colormap = DefaultColormap(Screen->display, DefaultScreen(Screen->display));
    if ( !XAllocNamedColor(Screen->display, colormap, "black", &black, &dummy) )
    {
      return; // color alloc failed, give up
    }
    bm_no = XCreateBitmapFromData(Screen->display, Screen->window, bm_no_data, 8, 8);
    no_ptr = XCreatePixmapCursor(Screen->display, bm_no, bm_no, &black, &black, 0, 0);
    XDefineCursor(Screen->display, Screen->window, no_ptr);
    XFreeCursor(Screen->display, no_ptr);
    if (bm_no != None)
        XFreePixmap(Screen->display, bm_no);
    XFreeColors(Screen->display,colormap,&black.pixel,1,0);
}

void vo_showcursor_S (tScreenOutput *Screen)
{
    if (WinID == 0)
        return;
    XDefineCursor(Screen->display, Screen->window, 0);
}

static int x11_errorhandler(Display *display, XErrorEvent * event)
{
#define MSGLEN 60
    char msg[MSGLEN];

    XGetErrorText(display, event->error_code, (char *) &msg, MSGLEN);

    mp_msg(MSGT_VO, MSGL_ERR, "X11 error: %s\n", msg);

    mp_msg(MSGT_VO, MSGL_V,
           "Type: %x, display: %p, resourceid: %lx, serial: %lx\n",
           event->type, event->display, event->resourceid, event->serial);
    mp_msg(MSGT_VO, MSGL_V,
           "Error code: %x, request code: %x, minor code: %x\n",
           event->error_code, event->request_code, event->minor_code);

//    abort();
    //exit_player("X11 error");
    return 0;
#undef MSGLEN
}

#ifndef CONFIG_X11
void fstype_help(void)
{
    mp_msg(MSGT_VO, MSGL_INFO, MSGTR_AvailableFsType);
    mp_msg(MSGT_IDENTIFY, MSGL_INFO, "ID_FULL_SCREEN_TYPES\n");

    mp_msg(MSGT_VO, MSGL_INFO, "    %-15s %s\n", "none",
           "don't set fullscreen window layer");
    mp_msg(MSGT_VO, MSGL_INFO, "    %-15s %s\n", "layer",
           "use _WIN_LAYER hint with default layer");
    mp_msg(MSGT_VO, MSGL_INFO, "    %-15s %s\n", "layer=<0..15>",
           "use _WIN_LAYER hint with a given layer number");
    mp_msg(MSGT_VO, MSGL_INFO, "    %-15s %s\n", "netwm",
           "force NETWM style");
    mp_msg(MSGT_VO, MSGL_INFO, "    %-15s %s\n", "above",
           "use _NETWM_STATE_ABOVE hint if available");
    mp_msg(MSGT_VO, MSGL_INFO, "    %-15s %s\n", "below",
           "use _NETWM_STATE_BELOW hint if available");
    mp_msg(MSGT_VO, MSGL_INFO, "    %-15s %s\n", "fullscreen",
           "use _NETWM_STATE_FULLSCREEN hint if availale");
    mp_msg(MSGT_VO, MSGL_INFO, "    %-15s %s\n", "stays_on_top",
           "use _NETWM_STATE_STAYS_ON_TOP hint if available");
    mp_msg(MSGT_VO, MSGL_INFO,
           "You can also negate the settings with simply putting '-' in the beginning");
    mp_msg(MSGT_VO, MSGL_INFO, "\n");
}
#endif

static void fstype_dump(int fstype)
{
    if (fstype)
    {
        mp_msg(MSGT_VO, MSGL_V, "[x11] Current fstype setting honours");
        if (fstype & vo_wm_LAYER)
            mp_msg(MSGT_VO, MSGL_V, " LAYER");
        if (fstype & vo_wm_FULLSCREEN)
            mp_msg(MSGT_VO, MSGL_V, " FULLSCREEN");
        if (fstype & vo_wm_STAYS_ON_TOP)
            mp_msg(MSGT_VO, MSGL_V, " STAYS_ON_TOP");
        if (fstype & vo_wm_ABOVE)
            mp_msg(MSGT_VO, MSGL_V, " ABOVE");
        if (fstype & vo_wm_BELOW)
            mp_msg(MSGT_VO, MSGL_V, " BELOW");
        mp_msg(MSGT_VO, MSGL_V, " X atoms\n");
    } else
        mp_msg(MSGT_VO, MSGL_V,
               "[x11] Current fstype setting doesn't honour any X atoms\n");
}

static int net_wm_support_state_test(Atom atom)
{
#define NET_WM_STATE_TEST(x) { if (atom == XA_NET_WM_STATE_##x) { mp_msg( MSGT_VO,MSGL_V, "[x11] Detected wm supports " #x " state.\n" ); return vo_wm_##x; } }

    NET_WM_STATE_TEST(FULLSCREEN);
    NET_WM_STATE_TEST(ABOVE);
    NET_WM_STATE_TEST(STAYS_ON_TOP);
    NET_WM_STATE_TEST(BELOW);
    return 0;
}

static int x11_get_property_S(Atom type, Atom ** args, unsigned long *nitems, tScreenOutput *Screen)
{
    int format;
    unsigned long bytesafter;

    return  Success ==
            XGetWindowProperty(Screen->display, Screen->parent, type, 0, 16384, False,
                               AnyPropertyType, &type, &format, nitems,
                               &bytesafter, (unsigned char **) args)
            && *nitems > 0;
}

static int vo_wm_detect_S(tScreenOutput *Screen)
{
    int i;
    int wm = 0;
    unsigned long nitems;
    Atom *args = NULL;

    if (WinID >= 0)
        return 0;

// -- supports layers
    if (x11_get_property_S(XA_WIN_PROTOCOLS, &args, &nitems, Screen))
    {
        mp_msg(MSGT_VO, MSGL_V, "[x11] Detected wm supports layers.\n");
        for (i = 0; i < nitems; i++)
        {
            if (args[i] == XA_WIN_LAYER)
            {
                wm |= vo_wm_LAYER;
                metacity_hack |= 1;
            } else
                /* metacity is the only window manager I know which reports
                 * supporting only the _WIN_LAYER hint in _WIN_PROTOCOLS.
                 * (what's more support for it is broken) */
                metacity_hack |= 2;
        }
        XFree(args);
        if (wm && (metacity_hack == 1))
        {
            // metacity claims to support layers, but it is not the truth :-)
            wm ^= vo_wm_LAYER;
            mp_msg(MSGT_VO, MSGL_V,
                   "[x11] Using workaround for Metacity bugs.\n");
        }
    }
// --- netwm
    if (x11_get_property_S(XA_NET_SUPPORTED, &args, &nitems, Screen))
    {
        mp_msg(MSGT_VO, MSGL_V, "[x11] Detected wm supports NetWM.\n");
        for (i = 0; i < nitems; i++)
            wm |= net_wm_support_state_test(args[i]);
        XFree(args);
    }

    if (wm == 0)
        mp_msg(MSGT_VO, MSGL_V, "[x11] Unknown wm type...\n");
    return wm;
}

static void init_atoms_S(tScreenOutput *Screen)
{
    XA_INIT(_NET_SUPPORTED);
    XA_INIT(_NET_WM_STATE);
    XA_INIT(_NET_WM_STATE_FULLSCREEN);
    XA_INIT(_NET_WM_STATE_ABOVE);
    XA_INIT(_NET_WM_STATE_STAYS_ON_TOP);
    XA_INIT(_NET_WM_STATE_BELOW);
    XA_INIT(_NET_WM_PID);
    XA_INIT(_WIN_PROTOCOLS);
    XA_INIT(_WIN_LAYER);
    XA_INIT(_WIN_HINTS);
    XA_INIT(WM_PROTOCOLS);
    XA_INIT(WM_DELETE_WINDOW);
}

void update_xinerama_info_S(tScreenOutput *Screen) {
    int screen = xinerama_screen;
    xinerama_x = xinerama_y = 0;
#ifdef CONFIG_XINERAMA
    if (screen >= -1 && XineramaIsActive(Screen->display))
    {
        XineramaScreenInfo *screens;
        int num_screens;

        screens = XineramaQueryScreens(Screen->display, &num_screens);
        if (screen >= num_screens)
            screen = num_screens - 1;
        if (screen == -1) {
            int x = vo_dx + vo_dwidth / 2;
            int y = vo_dy + vo_dheight / 2;
            for (screen = num_screens - 1; screen >= 0; screen--) {
               int left = screens[screen].x_org;
               int right = left + screens[screen].width;
               int top = screens[screen].y_org;
               int bottom = top + screens[screen].height;
               mp_msg(MSGT_VO, MSGL_INFO, "xinerama: {%i %i %i %i %i}\n", 
			       screen,
			       left,
			       right,
			       top,
			       bottom
			       );
               if (left <= x && x <= right && top <= y && y <= bottom)
                   break;
            }
        }
        if (screen < 0)
            screen = 0;
        Screen->vo_screenwidth = screens[screen].width;
        Screen->vo_screenheight = screens[screen].height;
        xinerama_x = screens[screen].x_org;
        xinerama_y = screens[screen].y_org;

        XFree(screens);
    }
#endif
    aspect_save_screenres(Screen->vo_screenwidth, Screen->vo_screenheight);
}

static
int setDisplay(const char *DISPLAYNAME, tScreenOutput *Screen) {
    char *dispName;

    if(!(Screen->displayName = getenv(DISPLAYNAME)))
	Screen->displayName = strdup(":0.0");
    dispName = XDisplayName(Screen->displayName);
    mp_msg(MSGT_VO, MSGL_V, "X11 opening display (%s;%s)\n", DISPLAYNAME, dispName);
    Screen->display = XOpenDisplay(dispName);
    if (!Screen->display)
    {
        mp_msg(MSGT_VO, MSGL_ERR,
               "vo: couldn't open the X11 display (%s;%s)!\n", DISPLAYNAME, dispName);
        return 0;
    }
    return -1;
}

static
int vo_s_init_S(const char *DISPLAY, tScreenOutput *Screen, tSide side);

int vo_s_init(void) {
    int res = vo_s_init_S("DISPLAYL", &ScreenLeft, LEFT) && vo_s_init_S("DISPLAYR", &ScreenRight, RIGHT);
    mp_msg(MSGT_VO, MSGL_V, "Estereoscopic: enabled\n");
    if (strcmp(ScreenLeft.displayName, ScreenRight.displayName) == 0) {
       mp_msg(MSGT_VO, MSGL_V, "Unique display: enabled\n");
       uniqueDisplay = 1;
    } else {
       mp_msg(MSGT_VO, MSGL_V, "Unique display: disable\n");
       uniqueDisplay = 0;
    }
    return res;
}

// TODO: Check vo_rootwin, WinID
static
int vo_s_init_S(const char *DISPLAY, tScreenOutput *Screen, tSide side)
{
    int depth, bpp;
    unsigned int mask;

    XImage *mXImage = NULL;

    XWindowAttributes attribs;
    char *dispName;

    initScreen(Screen, side);

    if (vo_rootwin) WinID = 0; // use root window

    if (Screen->vo_bpp)
    {
        // saver_off(Screen->display);
        saver_off_S(Screen);
        return 1;               // already called
    }

    XSetErrorHandler(x11_errorhandler);


    if (setDisplay(DISPLAY, Screen) == 0)
	return 0;
    Screen->screen = DefaultScreen(Screen->display);  // screen ID
    Screen->parent = RootWindow(Screen->display, Screen->screen);   // root window ID

    dispName = XDisplayName(Screen->displayName);

    init_atoms_S(Screen);

#ifdef CONFIG_XF86VM
    {
        int clock;

        XF86VidModeGetModeLine(Screen->display, Screen->screen, &clock, &modeline);
        if (!Screen->vo_screenwidth)
            Screen->vo_screenwidth = modeline.hdisplay;
        if (!Screen->vo_screenheight)
            Screen->vo_screenheight = modeline.vdisplay;
    }
#endif
    {
        if (!Screen->vo_screenwidth)
            Screen->vo_screenwidth = DisplayWidth(Screen->display, Screen->screen);
        if (!Screen->vo_screenheight)
            Screen->vo_screenheight = DisplayHeight(Screen->display, Screen->screen);
    }
    // get color depth (from root window, or the best visual):
    XGetWindowAttributes(Screen->display, Screen->parent, &attribs);
    depth = attribs.depth;

    if (depth != 15 && depth != 16 && depth != 24 && depth != 32)
    {
        Visual *visual;
        depth = vo_find_depth_from_visuals_S(Screen, &visual);
        if (depth != -1)
            mXImage = XCreateImage(Screen->display, visual, depth, ZPixmap,
                                   0, NULL, 1, 1, 8, 1);
    } else
        mXImage =
            XGetImage(Screen->display, Screen->parent, 0, 0, 1, 1, AllPlanes, ZPixmap);

    Screen->vo_bpp = depth;   // display depth on screen

    // get bits/pixel from XImage structure:
    if (mXImage == NULL)
    {
        mask = 0;
    } else
    {
        /*
         * for the depth==24 case, the XImage structures might use
         * 24 or 32 bits of data per pixel.  The global variable
         * Screen->vo_bpp stores the amount of data per pixel in the
         * XImage structure!
         *
         * Maybe we should rename Screen->vo_bpp to (or add) vo_bpp?
         */
        bpp = mXImage->bits_per_pixel;
        if ((Screen->vo_bpp + 7) / 8 != (bpp + 7) / 8)
            Screen->vo_bpp = bpp;     // by A'rpi
        mask =
            mXImage->red_mask | mXImage->green_mask | mXImage->blue_mask;
        mp_msg(MSGT_VO, MSGL_V,
               "vo: X11 color mask:  %X  (R:%lX G:%lX B:%lX)\n", mask,
               mXImage->red_mask, mXImage->green_mask, mXImage->blue_mask);
        XDestroyImage(mXImage);
    }
    if (((Screen->vo_bpp + 7) / 8) == 2)
    {
        if (mask == 0x7FFF)
            Screen->vo_bpp = 15;
        else if (mask == 0xFFFF)
            Screen->vo_bpp = 16;
    }
// XCloseDisplay( display );
/* slightly improved local display detection AST */
    if (strncmp(dispName, "unix:", 5) == 0)
        dispName += 4;
    else if (strncmp(dispName, "localhost:", 10) == 0)
        dispName += 9;
    if (*dispName == ':' && atoi(dispName + 1) < 10)
        mLocalDisplay = 1;
    else
        mLocalDisplay = 0;
    mp_msg(MSGT_VO, MSGL_V,
           "vo: X11 running at %dx%d with depth %d and %d bpp (\"%s\" => %s display)\n",
           Screen->vo_screenwidth, Screen->vo_screenheight, depth, Screen->vo_bpp,
           dispName, mLocalDisplay ? "local" : "remote");

    vo_wm_type = vo_wm_detect_S(Screen);

    vo_fs_type = vo_x11s_get_fs_type(vo_wm_type);

    fstype_dump(vo_fs_type);

    saver_off_S(Screen);

    return 1;
}

void vo_uninit_S(tScreenOutput *Screen)
{
    if (!Screen->display)
    {
        mp_msg(MSGT_VO, MSGL_V,
               "vo: x11 uninit called but X11 not initialized..\n");
        return;
    }
// if( !Screen->vo_bpp ) return;
    mp_msg(MSGT_VO, MSGL_V, "vo: uninit ...\n");
    XSetErrorHandler(NULL);
    XCloseDisplay(Screen->display);
    Screen->vo_bpp = 0;
    Screen->display = NULL;
}

#include "osdep/keycodes.h"
#include "wskeys.h"

#ifdef XF86XK_AudioPause
static const struct mp_keymap keysym_map[] = {
    {XF86XK_MenuKB, KEY_MENU},
    {XF86XK_AudioPlay, KEY_PLAY}, {XF86XK_AudioPause, KEY_PAUSE}, {XF86XK_AudioStop, KEY_STOP},
    {XF86XK_AudioPrev, KEY_PREV}, {XF86XK_AudioNext, KEY_NEXT},
    {XF86XK_AudioMute, KEY_MUTE}, {XF86XK_AudioLowerVolume, KEY_VOLUME_DOWN}, {XF86XK_AudioRaiseVolume, KEY_VOLUME_UP},
    {0, 0}
};

static void vo_x11s_putkey_ext(int keysym)
{
    int mpkey = lookup_keymap_table(keysym_map, keysym);
    if (mpkey)
        mplayer_put_key(mpkey);
}
#endif

static const struct mp_keymap keymap[] = {
    // special keys
    {wsEscape, KEY_ESC}, {wsBackSpace, KEY_BS}, {wsTab, KEY_TAB}, {wsEnter, KEY_ENTER},

    // cursor keys
    {wsLeft, KEY_LEFT}, {wsRight, KEY_RIGHT}, {wsUp, KEY_UP}, {wsDown, KEY_DOWN},

    // navigation block
    {wsInsert, KEY_INSERT}, {wsDelete, KEY_DELETE}, {wsHome, KEY_HOME}, {wsEnd, KEY_END},
    {wsPageUp, KEY_PAGE_UP}, {wsPageDown, KEY_PAGE_DOWN},

    // F-keys
    {wsF1, KEY_F+1}, {wsF2, KEY_F+2}, {wsF3, KEY_F+3}, {wsF4, KEY_F+4},
    {wsF5, KEY_F+5}, {wsF6, KEY_F+6}, {wsF7, KEY_F+7}, {wsF8, KEY_F+8},
    {wsF9, KEY_F+9}, {wsF10, KEY_F+10}, {wsF11, KEY_F+11}, {wsF12, KEY_F+12},

    // numpad independent of numlock
    {wsGrayMinus, '-'}, {wsGrayPlus, '+'}, {wsGrayMul, '*'}, {wsGrayDiv, '/'},
    {wsGrayEnter, KEY_KPENTER},

    // numpad with numlock
    {wsGray0, KEY_KP0}, {wsGray1, KEY_KP1}, {wsGray2, KEY_KP2},
    {wsGray3, KEY_KP3}, {wsGray4, KEY_KP4}, {wsGray5, KEY_KP5},
    {wsGray6, KEY_KP6}, {wsGray7, KEY_KP7}, {wsGray8, KEY_KP8},
    {wsGray9, KEY_KP9}, {wsGrayDecimal, KEY_KPDEC},

    // numpad without numlock
    {wsGrayInsert, KEY_KPINS}, {wsGrayEnd, KEY_KP1}, {wsGrayDown, KEY_KP2},
    {wsGrayPgDn, KEY_KP3}, {wsGrayLeft, KEY_KP4}, {wsGray5Dup, KEY_KP5},
    {wsGrayRight, KEY_KP6}, {wsGrayHome, KEY_KP7}, {wsGrayUp, KEY_KP8},
    {wsGrayPgUp, KEY_KP9}, {wsGrayDelete, KEY_KPDEL},

    {0, 0}
};

void vo_x11s_putkey(int key)
{
    static const char *passthrough_keys = " -+*/<>`~!@#$%^&()_{}:;\"\',.?\\|=[]";
    int mpkey = 0;
    if ((key >= 'a' && key <= 'z') ||
        (key >= 'A' && key <= 'Z') ||
        (key >= '0' && key <= '9') ||
        (key >  0   && key <  256 && strchr(passthrough_keys, key)))
        mpkey = key;

    if (!mpkey)
        mpkey = lookup_keymap_table(keymap, key);

    if (mpkey)
        mplayer_put_key(mpkey);
}


// ----- Motif header: -------

#define MWM_HINTS_FUNCTIONS     (1L << 0)
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_HINTS_INPUT_MODE    (1L << 2)
#define MWM_HINTS_STATUS        (1L << 3)

#define MWM_FUNC_ALL            (1L << 0)
#define MWM_FUNC_RESIZE         (1L << 1)
#define MWM_FUNC_MOVE           (1L << 2)
#define MWM_FUNC_MINIMIZE       (1L << 3)
#define MWM_FUNC_MAXIMIZE       (1L << 4)
#define MWM_FUNC_CLOSE          (1L << 5)

#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_BORDER        (1L << 1)
#define MWM_DECOR_RESIZEH       (1L << 2)
#define MWM_DECOR_TITLE         (1L << 3)
#define MWM_DECOR_MENU          (1L << 4)
#define MWM_DECOR_MINIMIZE      (1L << 5)
#define MWM_DECOR_MAXIMIZE      (1L << 6)

#define MWM_INPUT_MODELESS 0
#define MWM_INPUT_PRIMARY_APPLICATION_MODAL 1
#define MWM_INPUT_SYSTEM_MODAL 2
#define MWM_INPUT_FULL_APPLICATION_MODAL 3
#define MWM_INPUT_APPLICATION_MODAL MWM_INPUT_PRIMARY_APPLICATION_MODAL

#define MWM_TEAROFF_WINDOW      (1L<<0)

typedef struct
{
    long flags;
    long functions;
    long decorations;
    long input_mode;
    long state;
} MotifWmHints;

static MotifWmHints vo_MotifWmHints;
static Atom vo_MotifHints = None;

void vo_x11s_decoration_S( tScreenOutput *Screen, int d)
{
    static unsigned int olddecor = MWM_DECOR_ALL;
    static unsigned int oldfuncs =
        MWM_FUNC_MOVE | MWM_FUNC_CLOSE | MWM_FUNC_MINIMIZE |
        MWM_FUNC_MAXIMIZE | MWM_FUNC_RESIZE;
    Atom mtype;
    int mformat;
    unsigned long mn, mb;

    mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: vo_x11s_decoration {\n");

    if (!WinID)
        return;

    if (vo_fsmode & 8)
    {
        XSetTransientForHint(Screen->display, Screen->window,
                             RootWindow(Screen->display, Screen->screen));
    }

    vo_MotifHints = XInternAtom(Screen->display, "_MOTIF_WM_HINTS", 0);
    if (vo_MotifHints != None)
    {
        if (!d)
        {
            MotifWmHints *mhints = NULL;

            XGetWindowProperty(Screen->display, Screen->window, vo_MotifHints, 0, 20, False,
                               vo_MotifHints, &mtype, &mformat, &mn,
                               &mb, (unsigned char **) &mhints);
            if (mhints)
            {
                if (mhints->flags & MWM_HINTS_DECORATIONS)
                    olddecor = mhints->decorations;
                if (mhints->flags & MWM_HINTS_FUNCTIONS)
                    oldfuncs = mhints->functions;
                XFree(mhints);
            }
        }

        memset(&vo_MotifWmHints, 0, sizeof(MotifWmHints));
        vo_MotifWmHints.flags =
            MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS;
        if (d)
        {
            vo_MotifWmHints.functions = oldfuncs;
            d = olddecor;
        }
#if 0
        vo_MotifWmHints.decorations =
            d | ((vo_fsmode & 2) ? 0 : MWM_DECOR_MENU);
#else
        vo_MotifWmHints.decorations =
            d | ((vo_fsmode & 2) ? MWM_DECOR_MENU : 0);
#endif
        XChangeProperty(Screen->display, Screen->window, vo_MotifHints, vo_MotifHints, 32,
                        PropModeReplace,
                        (unsigned char *) &vo_MotifWmHints,
                        (vo_fsmode & 4) ? 4 : 5);
    }
    mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: vo_x11s_decoration }\n");
}

void vo_x11s_classhint_S(tScreenOutput *Screen, const char *name)
{
    XClassHint wmClass;
    pid_t pid = getpid();

    wmClass.res_name = vo_winname ? vo_winname : name;
    wmClass.res_class = "MPlayer";
    XSetClassHint(Screen->display, Screen->window, &wmClass);
    XChangeProperty(Screen->display, Screen->window, XA_NET_WM_PID, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *) &pid, 1);
}
//XSizeHints vo_hint;

#ifdef CONFIG_GUI
void vo_setwindow_S(tScreenOutput *Screen, Window w, GC g)
{
    Screen->window = w;
    Screen->vo_gc = g;
}
#endif

void vo_x11s_uninit(void)
{
     vo_x11s_uninit_S(&ScreenLeft);
     vo_x11s_uninit_S(&ScreenRight);
}

void vo_x11s_uninit_S(tScreenOutput *Screen)
{
    saver_on_S(Screen);
    if (Screen->window != None)
        vo_showcursor_S(Screen);

    if (Screen->f_gc)
    {
        XFreeGC(Screen->display, Screen->f_gc);
        Screen->f_gc = NULL;
    }

#ifdef CONFIG_GUI
    /* destroy window only if it's not controlled by the GUI */
    if (!use_gui)
#endif
    {
        if (Screen->vo_gc)
        {
            XSetBackground(Screen->display, Screen->vo_gc, 0);
            XFreeGC(Screen->display, Screen->vo_gc);
            Screen->vo_gc = NULL;
        }
        if (Screen->window != None)
        {
            XClearWindow(Screen->display, Screen->window);
            if (WinID < 0)
            {
                XEvent xev;

                XUnmapWindow(Screen->display, Screen->window);
                XDestroyWindow(Screen->display, Screen->window);
                do
                {
                    XNextEvent(Screen->display, &xev);
                }
                while (xev.type != DestroyNotify
                       || xev.xdestroywindow.event != Screen->window);
            }
            Screen->window = None;
        }
        vo_fs = 0;
        Screen->vo_old_width = Screen->vo_old_height = 0;
    }
}

static unsigned int mouse_timer;
static int mouse_waiting_hide;
static int mouse_ignore_motion = 0;

int vo_x11s_check_events_S(tScreenOutput *Screen)
{
    int ret = 0;
    XEvent Event;
    char buf[100];
    KeySym keySym;
    static XComposeStatus stat;

// unsigned long  vo_KeyTable[512];

    if ((vo_mouse_autohide) && mouse_waiting_hide &&
                                 (GetTimerMS() - mouse_timer >= 1000)) {
        vo_hidecursor_S(Screen);
        mouse_waiting_hide = 0;
    }

    while (XPending(Screen->display))
    {
        XNextEvent(Screen->display, &Event);
#ifdef CONFIG_GUI
        if (use_gui)
        {
            guiGetEvent(0, (char *) &Event);
            if (Screen->window != Event.xany.window)
                continue;
        }
#endif
//       printf("\rEvent.type=%X  \n",Event.type);
        switch (Event.type)
        {
            case Expose:
                ret |= VO_EVENT_EXPOSE;
                break;
            case ConfigureNotify:
//         if (!vo_fs && (Event.xconfigure.width == vo_screenwidth || Event.xconfigure.height == vo_screenheight)) break;
//         if (vo_fs && Event.xconfigure.width != vo_screenwidth && Event.xconfigure.height != vo_screenheight) break;
                if (Screen->window == None)
                    break;
                {
                    int old_w = vo_dwidth, old_h = vo_dheight;
		    int old_x = vo_dx, old_y = vo_dy;
                    vo_x11s_update_geometry_S(Screen);
                    if (vo_dwidth != old_w || vo_dheight != old_h || vo_dx != old_x || vo_dy != old_y)
                        ret |= VO_EVENT_RESIZE;
                }
                break;
            case KeyPress:
                {
                    int key;

#ifdef CONFIG_GUI
                    if ( use_gui ) { break; }
#endif

                    XLookupString(&Event.xkey, buf, sizeof(buf), &keySym,
                                  &stat);
#ifdef XF86XK_AudioPause
                    vo_x11s_putkey_ext(keySym);
#endif
                    key =
                        ((keySym & 0xff00) !=
                         0 ? ((keySym & 0x00ff) + 256) : (keySym));
                    vo_x11s_putkey(key);
                    ret |= VO_EVENT_KEYPRESS;
                }
                break;
            case MotionNotify:
                if(enable_mouse_movements)
                {
                    char cmd_str[40];
                    sprintf(cmd_str,"set_mouse_pos %i %i",Event.xmotion.x, Event.xmotion.y);
                    mp_input_queue_cmd(mp_input_parse_cmd(cmd_str));
                }
		if (mouse_ignore_motion)
		  mouse_ignore_motion = 0;
		else
                if (vo_mouse_autohide)
                {
                    vo_showcursor_S(Screen);
                    mouse_waiting_hide = 1;
                    mouse_timer = GetTimerMS();
                }
                break;
            case ButtonPress:
                if (vo_mouse_autohide)
                {
                    vo_showcursor_S(Screen);
                    mouse_waiting_hide = 1;
                    mouse_timer = GetTimerMS();
                }
#ifdef CONFIG_GUI
                // Ignore mouse button 1-3 under GUI.
                if (use_gui && (Event.xbutton.button >= 1)
                    && (Event.xbutton.button <= 3))
                    break;
#endif
                mplayer_put_key((MOUSE_BTN0 + Event.xbutton.button -
                                 1) | MP_KEY_DOWN);
                break;
            case ButtonRelease:
                if (vo_mouse_autohide)
                {
                    vo_showcursor_S(Screen);
                    mouse_waiting_hide = 1;
                    mouse_timer = GetTimerMS();
                }
#ifdef CONFIG_GUI
                // Ignore mouse button 1-3 under GUI.
                if (use_gui && (Event.xbutton.button >= 1)
                    && (Event.xbutton.button <= 3))
                    break;
#endif
                mplayer_put_key(MOUSE_BTN0 + Event.xbutton.button - 1);
                break;
            case PropertyNotify:
                {
                    char *name =
                        XGetAtomName(Screen->display, Event.xproperty.atom);

                    if (!name)
                        break;

//          fprintf(stderr,"[ws] PropertyNotify ( 0x%x ) %s ( 0x%x )\n",window,name,Event.xproperty.atom );

                    XFree(name);
                }
                break;
            case MapNotify:
                Screen->vo_hint.win_gravity = old_gravity;
                XSetWMNormalHints(Screen->display, Screen->window, &Screen->vo_hint);
                vo_fs_flip = 0;
                break;
	    case ClientMessage:
                if (Event.xclient.message_type == XAWM_PROTOCOLS &&
                    Event.xclient.data.l[0] == XAWM_DELETE_WINDOW)
                    mplayer_put_key(KEY_CLOSE_WIN);
                break;
        }
    }
    return ret;
}

/**
 * \brief sets the size and position of the non-fullscreen window.
 */
void vo_x11s_nofs_sizepos_S(tScreenOutput *Screen, int x, int y, int width, int height)
{
  vo_x11s_sizehint_S(Screen, x, y, width, height, 0);
  if (vo_fs) {
    Screen->vo_old_x = x;
    Screen->vo_old_y = y;
    Screen->vo_old_width = width;
    Screen->vo_old_height = height;
  }
  else
  {
   vo_dwidth = width;
   vo_dheight = height;

    mp_msg(MSGT_VO, MSGL_INFO, "vo_x11s_nofs_sizepos_S: {%i %i %i %i}\n", 
		x, y,
		width, height);

   XMoveResizeWindow(Screen->display, Screen->window, x, y, width, height);
  }
}

void vo_x11s_sizehint_S(tScreenOutput *Screen, int x, int y, int width, int height, int max)
{
    Screen->vo_hint.flags = 0;
    if (vo_keepaspect)
    {
        Screen->vo_hint.flags |= PAspect;
        Screen->vo_hint.min_aspect.x = width;
        Screen->vo_hint.min_aspect.y = height;
        Screen->vo_hint.max_aspect.x = width;
        Screen->vo_hint.max_aspect.y = height;
    }

    Screen->vo_hint.flags |= PPosition | PSize;
    Screen->vo_hint.x = x;
    Screen->vo_hint.y = y;
    Screen->vo_hint.width = width;
    Screen->vo_hint.height = height;
    if (max)
    {
        Screen->vo_hint.flags |= PMaxSize;
        Screen->vo_hint.max_width = width;
        Screen->vo_hint.max_height = height;
    } else
    {
        Screen->vo_hint.max_width = 0;
        Screen->vo_hint.max_height = 0;
    }

    // Set minimum height/width to 4 to avoid off-by-one errors
    // and because mga_vid requires a minimal size of 4 pixels.
    Screen->vo_hint.flags |= PMinSize;
    Screen->vo_hint.min_width = Screen->vo_hint.min_height = 4;

    // Set the base size. A window manager might display the window
    // size to the user relative to this.
    // Setting these to width/height might be nice, but e.g. fluxbox can't handle it.
    Screen->vo_hint.flags |= PBaseSize;
    Screen->vo_hint.base_width = 0 /*width*/;
    Screen->vo_hint.base_height = 0 /*height*/;

    Screen->vo_hint.flags |= PWinGravity;
    Screen->vo_hint.win_gravity = StaticGravity;
    XSetWMNormalHints(Screen->display, Screen->window, &Screen->vo_hint);
}

static int vo_x11s_get_gnome_layer_S(tScreenOutput *Screen)
{
    Atom type;
    int format;
    unsigned long nitems;
    unsigned long bytesafter;
    unsigned short *args = NULL;

    if (XGetWindowProperty(Screen->display, Screen->window, XA_WIN_LAYER, 0, 16384,
                           False, AnyPropertyType, &type, &format, &nitems,
                           &bytesafter,
                           (unsigned char **) &args) == Success
        && nitems > 0 && args)
    {
        mp_msg(MSGT_VO, MSGL_V, "[x11] original window layer is %d.\n",
               *args);
        return *args;
    }
    return WIN_LAYER_NORMAL;
}

Window vo_x11s_create_smooth_window_S(tScreenOutput *Screen,
                                   Visual * vis, int x, int y,
                                   unsigned int width, unsigned int height,
                                   int depth, Colormap col_map)
{
    unsigned long xswamask = CWBorderPixel;
    XSetWindowAttributes xswa;
    Window ret_win;
    mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: create_smooth_window {\n");

    if (col_map != CopyFromParent)
    {
        xswa.colormap = col_map;
        xswamask |= CWColormap;
    }
    xswa.background_pixel = 0;
    xswa.border_pixel = 0;
    xswa.backing_store = NotUseful;
    xswa.bit_gravity = StaticGravity;

    ret_win =
        XCreateWindow(Screen->display, Screen->parent, x, y, width, height, 0, depth,
                      CopyFromParent, vis, xswamask, &xswa);
    XSetWMProtocols(Screen->display, ret_win, &XAWM_DELETE_WINDOW, 1);
    if (!Screen->f_gc)
        Screen->f_gc = XCreateGC(Screen->display, ret_win, 0, 0);
    XSetForeground(Screen->display, Screen->f_gc, 0);

    XSync(Screen->display, False);

    mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: create_smooth_window }\n");
    return ret_win;
}

/**
 * \brief create and setup a window suitable for display
 * \param vis Visual to use for creating the window
 * \param x x position of window
 * \param y y position of window
 * \param width width of window
 * \param height height of window
 * \param flags flags for window creation.
 *              Only VOFLAG_FULLSCREEN is supported so far.
 * \param col_map Colourmap for window or CopyFromParent if a specific colormap isn't needed
 * \param classname name to use for the classhint
 * \param title title for the window
 *
 * This also does the grunt-work like setting Window Manager hints etc.
 * If window is already set it just moves and resizes it.
 */
// TODO: El vo_gc_L no esta bien actualizado.
void vo_x11s_create_vo_window_S(tScreenOutput *Screen, int x, int y,
                             unsigned int width, unsigned int height, int flags,
                             Colormap col_map,
                             const char *classname, const char *title)
{
  XGCValues xgcv;

  mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: vo_x11s_create_window_S {\n");
  mp_msg(MSGT_VO, MSGL_INFO, "MOLDEO: vo_fs =  %i\n", vo_fs);

  if (WinID >= 0) {
    vo_fs = flags & VOFLAG_FULLSCREEN;
    Screen->window = WinID ? (Window)WinID : Screen->parent;
    if (col_map != CopyFromParent) {
      unsigned long xswamask = CWColormap;
      XSetWindowAttributes xswa;
      xswa.colormap = col_map;
      XChangeWindowAttributes(Screen->display, Screen->window, xswamask, &xswa);
      XInstallColormap(Screen->display, col_map);
    }
    if (WinID)
	    vo_x11s_update_geometry_S(Screen);
    vo_x11s_selectinput_witherr_S(Screen,
          StructureNotifyMask | KeyPressMask | PointerMotionMask |
          ButtonPressMask | ButtonReleaseMask | ExposureMask);
    goto final;
  }
  if (Screen->window == None) {
    XSizeHints hint;
    XEvent xev;
    vo_fs = 0;
    vo_dwidth = width;
    vo_dheight = height;
    Screen->window = vo_x11s_create_smooth_window_S(Screen, Screen->vinfo.visual,
                      x, y, width, height, Screen->vinfo.depth, col_map);
    vo_x11s_classhint_S(Screen, classname);
    XStoreName(Screen->display, Screen->window, title);
    vo_hidecursor_S(Screen);
    XSelectInput(Screen->display, Screen->window, StructureNotifyMask);
    hint.x = x; hint.y = y;
    hint.width = width; hint.height = height;
    hint.flags = PPosition | PSize;
    XSetStandardProperties(Screen->display, Screen->window, title, title, None, NULL, 0, &hint);
    vo_x11s_sizehint_S(Screen, x, y, width, height, 0);
    if (!vo_border) vo_x11s_decoration_S(Screen, 0);
    // map window
    XMapWindow(Screen->display, Screen->window);
    XClearWindow(Screen->display, Screen->window);
    // wait for map
    do {
      XNextEvent(Screen->display, &xev);
    } while (xev.type != MapNotify || xev.xmap.event != Screen->window);
    XSelectInput(Screen->display, Screen->window, NoEventMask);
    XSync(Screen->display, False);
    vo_x11s_selectinput_witherr_S(Screen,
          StructureNotifyMask | KeyPressMask | PointerMotionMask |
          ButtonPressMask | ButtonReleaseMask | ExposureMask);
  }
  if (vo_ontop)
	  vo_x11s_setlayer_S(Screen, vo_ontop);
  vo_x11s_nofs_sizepos_S(Screen, vo_dx, vo_dy, width, height);
  if (!!vo_fs != !!(flags & VOFLAG_FULLSCREEN))
	  vo_x11s_fullscreen_S(Screen, vo_fs);
  else if (vo_fs) {
    // if we are already in fullscreen do not switch back and forth, just
    // set the size values right.
    vo_dwidth  = Screen->vo_screenwidth;
    vo_dheight = Screen->vo_screenheight;
  }
final:
  if (Screen->vo_gc != None)
	  XFreeGC(Screen->display, Screen->vo_gc);
  Screen->vo_gc = XCreateGC(Screen->display, Screen->window, GCForeground, &xgcv);
  XSync(Screen->display, False);
  vo_mouse_autohide = 1;

  mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: vo_x11s_create_window_S }\n");
}

void vo_x11s_clearwindow_part_S(tScreenOutput *Screen,
                             int img_width, int img_height, int use_fs)
{
    int u_dheight, u_dwidth, left_ov, left_ov2;

    if (!Screen->f_gc)
        return;

    u_dheight = use_fs ? Screen->vo_screenheight : vo_dheight;
    u_dwidth = use_fs ? Screen->vo_screenwidth : vo_dwidth;
    if ((u_dheight <= img_height) && (u_dwidth <= img_width))
        return;

    left_ov = (u_dheight - img_height) / 2;
    left_ov2 = (u_dwidth - img_width) / 2;

    XFillRectangle(Screen->display, Screen->window, Screen->f_gc, 0, 0, u_dwidth, left_ov);
    XFillRectangle(Screen->display, Screen->window, Screen->f_gc, 0, u_dheight - left_ov - 1,
                   u_dwidth, left_ov + 1);

    if (u_dwidth > img_width)
    {
        XFillRectangle(Screen->display, Screen->window, Screen->f_gc, 0, left_ov, left_ov2,
                       img_height);
        XFillRectangle(Screen->display, Screen->window, Screen->f_gc, u_dwidth - left_ov2 - 1,
                       left_ov, left_ov2 + 1, img_height);
    }

    XFlush(Screen->display);
}

void vo_x11s_clearwindow_S(tScreenOutput *Screen)
{
    if (!Screen->f_gc)
        return;
    XFillRectangle(Screen->display, Screen->window, Screen->f_gc, 0, 0, Screen->vo_screenwidth,
                   Screen->vo_screenheight);
    //
    XFlush(Screen->display);
}


void vo_x11s_setlayer_S(tScreenOutput *Screen, int layer)
{
    if (WinID >= 0)
        return;

    if (vo_fs_type & vo_wm_LAYER)
    {
        XClientMessageEvent xev;

        if (!orig_layer)
            orig_layer = vo_x11s_get_gnome_layer_S(Screen);

        memset(&xev, 0, sizeof(xev));
        xev.type = ClientMessage;
        xev.display = Screen->display;
        xev.window = Screen->window;
        xev.message_type = XA_WIN_LAYER;
        xev.format = 32;
        xev.data.l[0] = layer ? fs_layer : orig_layer;  // if not fullscreen, stay on default layer
        xev.data.l[1] = CurrentTime;
        mp_msg(MSGT_VO, MSGL_V,
               "[x11] Layered style stay on top (layer %ld).\n",
               xev.data.l[0]);
        XSendEvent(Screen->display, Screen->parent, False, SubstructureNotifyMask,
                   (XEvent *) & xev);
    } else if (vo_fs_type & vo_wm_NETWM)
    {
        XClientMessageEvent xev;
        char *state;

        memset(&xev, 0, sizeof(xev));
        xev.type = ClientMessage;
        xev.message_type = XA_NET_WM_STATE;
        xev.display = Screen->display;
        xev.window = Screen->window;
        xev.format = 32;
        xev.data.l[0] = layer;

        if (vo_fs_type & vo_wm_STAYS_ON_TOP)
            xev.data.l[1] = XA_NET_WM_STATE_STAYS_ON_TOP;
        else if (vo_fs_type & vo_wm_ABOVE)
            xev.data.l[1] = XA_NET_WM_STATE_ABOVE;
        else if (vo_fs_type & vo_wm_FULLSCREEN)
            xev.data.l[1] = XA_NET_WM_STATE_FULLSCREEN;
        else if (vo_fs_type & vo_wm_BELOW)
            // This is not fallback. We can safely assume that the situation
            // where only NETWM_STATE_BELOW is supported doesn't exist.
            xev.data.l[1] = XA_NET_WM_STATE_BELOW;

        XSendEvent(Screen->display, Screen->parent, False, SubstructureRedirectMask,
                   (XEvent *) & xev);
        state = XGetAtomName(Screen->display, xev.data.l[1]);
        mp_msg(MSGT_VO, MSGL_V,
               "[x11] NET style stay on top (layer %d). Using state %s.\n",
               layer, state);
        XFree(state);
    }
}

static int vo_x11s_get_fs_type(int supported)
{
    int i;
    int type = supported;

    if (vo_fstype_list)
    {
        for (i = 0; vo_fstype_list[i]; i++)
        {
            int neg = 0;
            char *arg = vo_fstype_list[i];

            if (vo_fstype_list[i][0] == '-')
            {
                neg = 1;
                arg = vo_fstype_list[i] + 1;
            }

            if (!strncmp(arg, "layer", 5))
            {
                if (!neg && (arg[5] == '='))
                {
                    char *endptr = NULL;
                    int layer = strtol(vo_fstype_list[i] + 6, &endptr, 10);

                    if (endptr && *endptr == '\0' && layer >= 0
                        && layer <= 15)
                        fs_layer = layer;
                }
                if (neg)
                    type &= ~vo_wm_LAYER;
                else
                    type |= vo_wm_LAYER;
            } else if (!strcmp(arg, "above"))
            {
                if (neg)
                    type &= ~vo_wm_ABOVE;
                else
                    type |= vo_wm_ABOVE;
            } else if (!strcmp(arg, "fullscreen"))
            {
                if (neg)
                    type &= ~vo_wm_FULLSCREEN;
                else
                    type |= vo_wm_FULLSCREEN;
            } else if (!strcmp(arg, "stays_on_top"))
            {
                if (neg)
                    type &= ~vo_wm_STAYS_ON_TOP;
                else
                    type |= vo_wm_STAYS_ON_TOP;
            } else if (!strcmp(arg, "below"))
            {
                if (neg)
                    type &= ~vo_wm_BELOW;
                else
                    type |= vo_wm_BELOW;
            } else if (!strcmp(arg, "netwm"))
            {
                if (neg)
                    type &= ~vo_wm_NETWM;
                else
                    type |= vo_wm_NETWM;
            } else if (!strcmp(arg, "none"))
                type = 0; // clear; keep parsing
        }
    }

    return type;
}

/**
 * \brief update vo_dx, vo_dy, vo_dwidth and vo_dheight with current values of window
 * \return returns current color depth of window
 */
int vo_x11s_update_geometry_S(tScreenOutput *Screen) {
    unsigned depth, w, h;
    int dummy_int;
    Window dummy_win;
    mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: update_geometry_S\n");
    XGetGeometry(Screen->display, Screen->window, &dummy_win, &dummy_int, &dummy_int,
                 &w, &h, &dummy_int, &depth);
    if (w <= INT_MAX && h <= INT_MAX) { vo_dwidth = w; vo_dheight = h; }
    XTranslateCoordinates(Screen->display, Screen->window, Screen->parent, 0, 0, &vo_dx, &vo_dy,
                          &dummy_win);
    if (vo_wintitle)
        XStoreName(Screen->display, Screen->window, Screen->vo_wintitle);

    mp_msg(MSGT_VO, MSGL_INFO, "MOLDEO: update_geometry_S (%i, %i)\n", vo_dx, vo_dy);

    return depth <= INT_MAX ? depth : 0;
}

void vo_x11s_fullscreen(void)
{
    mp_msg(MSGT_VO, MSGL_INFO, "fullscreen: {\n");
    mp_msg(MSGT_VO, MSGL_INFO, "vo_fs = %i\n", vo_fs);
    vo_x11s_fullscreen_S(&ScreenLeft, vo_fs);
    vo_x11s_fullscreen_S(&ScreenRight, vo_fs);
    if(vo_fs) {
	vo_fs = 0; 
	vo_border = 0;
        vo_fs_flip = 1;
    } else {
	vo_fs = -1;
	vo_border = -1;
        vo_fs_flip = 0;
    }
    mp_msg(MSGT_VO, MSGL_INFO, "vo_fs = %i\n", vo_fs);
    mp_msg(MSGT_VO, MSGL_INFO, "fullscreen: }\n");
}

void vo_x11s_fullscreen_S(tScreenOutput *Screen, int vo_fs)
{
    int x, y, w, h;

    mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: vo_x11s_fullscreen_S {\n");


    if (vo_fs_flip)
        return;

    if (vo_fs)
    {
        //vo_x11s_ewmh_fullscreen_S(Screen, _NET_WM_STATE_REMOVE);   // removes fullscreen state if wm supports EWMH
	    x = Screen->vo_old_x;
	    y = Screen->vo_old_y;
	    w = Screen->vo_old_width;
	    h = Screen->vo_old_height;
    } else
    {
        // win->fs
        //vo_x11s_ewmh_fullscreen_S(Screen, _NET_WM_STATE_ADD);      // sends fullscreen state to be added if wm supports EWMH

        if ( ! (vo_fs_type & vo_wm_FULLSCREEN) ) // not needed with EWMH fs
        {
            Screen->vo_old_x = vo_dx;
            Screen->vo_old_y = vo_dy;
            Screen->vo_old_width = vo_dwidth;
            Screen->vo_old_height = vo_dheight;
        }
        update_xinerama_info_S(Screen);
	w = Screen->vo_screenwidth / 2;
	h = Screen->vo_screenheight;
	// MOLDEO: Aqui acomodo el X y Y de la pantalla
        mp_msg(MSGT_VO, MSGL_INFO, "MOLDEO: vo_x11s_fullscreen_S {\n");
	if (Screen->side == LEFT) {
		x = xinerama_x;
		y = xinerama_y;
	} else {
		x = Screen->vo_screenwidth / 2;
		y = xinerama_y;
	}
    }
#if 0
    {
        long dummy;

        XGetWMNormalHints(Screen->display, Screen->window, &Screen->vo_hint, &dummy);
        if (!(Screen->vo_hint.flags & PWinGravity))
            old_gravity = NorthWestGravity;
        else
            old_gravity = Screen->vo_hint.win_gravity;
    }
#endif
    if (vo_wm_type == 0 && !(vo_fsmode & 16))
    {
        XUnmapWindow(Screen->display, Screen->window);      // required for MWM
        XWithdrawWindow(Screen->display, Screen->window, Screen->screen);
    }

    //if ( ! (vo_fs_type & vo_wm_FULLSCREEN) ) // not needed with EWMH fs
    //{
        vo_x11s_decoration_S(Screen, vo_border && !vo_fs);
        vo_x11s_sizehint_S(Screen, x, y, w, h, 0);
        vo_x11s_setlayer_S(Screen, vo_fs);

        XMoveResizeWindow(Screen->display, Screen->window, x, y, w, h);
    //}
    /* some WMs lose ontop after fullscreen */
    if ((!(vo_fs)) & vo_ontop)
        vo_x11s_setlayer_S(Screen, vo_ontop);

    mp_msg(MSGT_VO, MSGL_INFO, "fullscreen: {%i %i %i %i}\n", 
		x, y,
		w, h);

    XMapRaised(Screen->display, Screen->window);
    if ( ! (vo_fs_type & vo_wm_FULLSCREEN) ) // some WMs change window pos on map
        XMoveResizeWindow(Screen->display, Screen->window, x, y, w, h);
    XRaiseWindow(Screen->display, Screen->window);
    XFlush(Screen->display);
    mp_msg(MSGT_VO, MSGL_DBG2, "MOLDEO: vo_x11s_fullscreen_S }\n");
}

void vo_x11s_ontop(void)
{
    vo_ontop = (!(vo_ontop));
    vo_x11s_setlayer_S(&ScreenLeft, vo_ontop);
    vo_x11s_setlayer_S(&ScreenRight, vo_ontop);
}

void vo_x11s_border(void)
{
    vo_border = !vo_border;
    vo_x11s_decoration_S(&ScreenLeft, vo_border && !vo_fs);
    vo_x11s_decoration_S(&ScreenRight, vo_border && !vo_fs);
}

/*
 * XScreensaver stuff
 */

static int screensaver_off;
static unsigned int time_last;

void xscreensaver_heartbeat_S(tScreenOutput *Screen)
{
    unsigned int time = GetTimerMS();
    // static unsigned int last_warp_time = 0;
    // static int fake_move_distance = 16;

    if (Screen->display && screensaver_off && (time - time_last) > 30000)
    {
        time_last = time;

        XResetScreenSaver(Screen->display);
    }
}

static int xss_suspend(Bool suspend)
{
#ifndef CONFIG_XSS
    return 0;
#else
    int event, error, major, minor;
    if (XScreenSaverQueryExtension(ScreenLeft.display, &event, &error) != True ||
        XScreenSaverQueryVersion(ScreenLeft.display, &major, &minor) != True)
        return 0;
    if (major < 1 || (major == 1 && minor < 1))
        return 0;
    XScreenSaverSuspend(ScreenLeft.display, suspend);
    return 1;
#endif
}

/*
 * End of XScreensaver stuff
 */

void saver_on_S(tScreenOutput *Screen)
{

    if (!screensaver_off)
        return;
    screensaver_off = 0;
    if (xss_suspend(False))
        return;
#ifdef CONFIG_XDPMS
    if (dpms_disabled)
    {
        int nothing;
        if (DPMSQueryExtension(Screen->display, &nothing, &nothing))
        {
            if (!DPMSEnable(Screen->display))
            {                   // restoring power saving settings
                mp_msg(MSGT_VO, MSGL_WARN, "DPMS not available?\n");
            } else
            {
                // DPMS does not seem to be enabled unless we call DPMSInfo
                BOOL onoff;
                CARD16 state;

                DPMSForceLevel(Screen->display, DPMSModeOn);
                DPMSInfo(Screen->display, &state, &onoff);
                if (onoff)
                {
                    mp_msg(MSGT_VO, MSGL_V,
                           "Successfully enabled DPMS\n");
                } else
                {
                    mp_msg(MSGT_VO, MSGL_WARN, "Could not enable DPMS\n");
                }
            }
        }
        dpms_disabled = 0;
    }
#endif
}

void saver_off_S(tScreenOutput *Screen)
{
    int nothing;

    if (screensaver_off)
        return;
    screensaver_off = 1;
    if (xss_suspend(True))
        return;
#ifdef CONFIG_XDPMS
    if (DPMSQueryExtension(Screen->display, &nothing, &nothing))
    {
        BOOL onoff;
        CARD16 state;

        DPMSInfo(Screen->display, &state, &onoff);
        if (onoff)
        {
            Status stat;

            mp_msg(MSGT_VO, MSGL_V, "Disabling DPMS\n");
            dpms_disabled = 1;
            stat = DPMSDisable(Screen->display);       // monitor powersave off
            mp_msg(MSGT_VO, MSGL_V, "DPMSDisable stat: %d\n", stat);
        }
    }
#endif
}

static XErrorHandler old_handler = NULL;
static int selectinput_err = 0;
static int x11_selectinput_errorhandler(Display * display,
                                        XErrorEvent * event)
{
    if (event->error_code == BadAccess)
    {
        selectinput_err = 1;
        mp_msg(MSGT_VO, MSGL_ERR,
               "X11 error: BadAccess during XSelectInput Call\n");
        mp_msg(MSGT_VO, MSGL_ERR,
               "X11 error: The 'ButtonPressMask' mask of specified window has probably already used by another appication (see man XSelectInput)\n");
        /* If you think MPlayer should shutdown with this error,
         * comment out the following line */
        return 0;
    }
    if (old_handler != NULL)
        old_handler(display, event);
    else
        x11_errorhandler(display, event);
    return 0;
}

void vo_x11s_selectinput_witherr_S(tScreenOutput *Screen, long event_mask)
{
    XSync(Screen->display, False);
    old_handler = XSetErrorHandler(x11_selectinput_errorhandler);
    selectinput_err = 0;
    if (vo_nomouse_input)
    {
        XSelectInput(Screen->display, Screen->window,
                     event_mask &
                     (~(ButtonPressMask | ButtonReleaseMask)));
    } else
    {
        XSelectInput(Screen->display, Screen->window, event_mask);
    }
    XSync(Screen->display, False);
    XSetErrorHandler(old_handler);
    if (selectinput_err)
    {
        mp_msg(MSGT_VO, MSGL_ERR,
               "X11 error: MPlayer discards mouse control (reconfiguring)\n");
        XSelectInput(Screen->display, Screen->window,
                     event_mask &
                     (~
                      (ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask)));
    }
}

#ifdef CONFIG_XF86VM
void vo_vms_switch(void)
{
    int vm_event, vm_error;
    int vm_ver, vm_rev;
    int i, j, have_vm = 0;
    int X = vo_dwidth, Y = vo_dheight;
    int modeline_width, modeline_height;

    int modecount;

    if (XF86VidModeQueryExtension(ScreenLeft.display, &vm_event, &vm_error))
    {
        XF86VidModeQueryVersion(ScreenLeft.display, &vm_ver, &vm_rev);
        mp_msg(MSGT_VO, MSGL_V, "XF86VidMode extension v%i.%i\n", vm_ver,
               vm_rev);
        have_vm = 1;
    } else {
        mp_msg(MSGT_VO, MSGL_WARN,
               "XF86VidMode extension not available.\n");
    }

    if (have_vm)
    {
        if (vidmodes_s == NULL)
            XF86VidModeGetAllModeLines(ScreenLeft.display, ScreenLeft.screen, &modecount,
                                       &vidmodes_s);
        j = 0;
        modeline_width = vidmodes_s[0]->hdisplay;
        modeline_height = vidmodes_s[0]->vdisplay;

        for (i = 1; i < modecount; i++)
            if ((vidmodes_s[i]->hdisplay >= X)
                && (vidmodes_s[i]->vdisplay >= Y))
                if ((vidmodes_s[i]->hdisplay <= modeline_width)
                    && (vidmodes_s[i]->vdisplay <= modeline_height))
                {
                    modeline_width = vidmodes_s[i]->hdisplay;
                    modeline_height = vidmodes_s[i]->vdisplay;
                    j = i;
                }

        mp_msg(MSGT_VO, MSGL_INFO, MSGTR_SelectedVideoMode,
               modeline_width, modeline_height, X, Y);
        XF86VidModeLockModeSwitch(ScreenLeft.display, ScreenLeft.screen, 0);
        XF86VidModeSwitchToMode(ScreenLeft.display, ScreenLeft.screen, vidmodes_s[j]);
        XF86VidModeSwitchToMode(ScreenLeft.display, ScreenLeft.screen, vidmodes_s[j]);

        // FIXME: all this is more of a hack than proper solution
        X = (vo_screenwidth - modeline_width) / 2;
        Y = (vo_screenheight - modeline_height) / 2;
        XF86VidModeSetViewPort(ScreenLeft.display, ScreenLeft.screen, X, Y);
        vo_dx = X;
        vo_dy = Y;
        vo_dwidth = modeline_width;
        vo_dheight = modeline_height;
        aspect_save_screenres(modeline_width, modeline_height);
    }
}

void vo_vms_close(void)
{
#ifdef CONFIG_GUI
#ifdef MOLDEO_ESTEREOSCOPIC
#define window window_L
#endif
    if (vidmodes_s != NULL && window != None)
#ifdef MOLDEO_ESTEREOSCOPIC
#undef window
#endif
#else
    if (vidmodes_s != NULL)
#endif
    {
        int i, modecount;

        free(vidmodes_s);
        vidmodes_s = NULL;
        XF86VidModeGetAllModeLines(ScreenLeft.display, ScreenLeft.screen, &modecount,
                                   &vidmodes_s);
        for (i = 0; i < modecount; i++)
            if ((vidmodes_s[i]->hdisplay == vo_screenwidth)
                && (vidmodes_s[i]->vdisplay == vo_screenheight))
            {
                mp_msg(MSGT_VO, MSGL_INFO,
                       "Returning to original mode %dx%d\n",
                       vo_screenwidth, vo_screenheight);
                break;
            }

        XF86VidModeSwitchToMode(ScreenLeft.display, ScreenLeft.screen, vidmodes_s[i]);
        XF86VidModeSwitchToMode(ScreenLeft.display, ScreenLeft.screen, vidmodes_s[i]);
        free(vidmodes_s);
        vidmodes_s = NULL;
    }
}
#endif

#endif                          /* X11_FULLSCREEN */


/*
 * Scan the available visuals on this Display/Screen.  Try to find
 * the 'best' available TrueColor visual that has a decent color
 * depth (at least 15bit).  If there are multiple visuals with depth
 * >= 15bit, we prefer visuals with a smaller color depth.
 */
int vo_find_depth_from_visuals_S(tScreenOutput *Screen, Visual **visual_return)
{
    XVisualInfo visual_tmpl;
    XVisualInfo *visuals;
    int nvisuals, i;
    int bestvisual = -1;
    int bestvisual_depth = -1;

    visual_tmpl.screen = Screen->screen;
    visual_tmpl.class = TrueColor;
    visuals = XGetVisualInfo(Screen->display,
                             VisualScreenMask | VisualClassMask,
                             &visual_tmpl, &nvisuals);
    if (visuals != NULL)
    {
        for (i = 0; i < nvisuals; i++)
        {
            mp_msg(MSGT_VO, MSGL_V,
                   "vo: X11 truecolor visual %#lx, depth %d, R:%lX G:%lX B:%lX\n",
                   visuals[i].visualid, visuals[i].depth,
                   visuals[i].red_mask, visuals[i].green_mask,
                   visuals[i].blue_mask);
            /*
             * Save the visual index and its depth, if this is the first
             * truecolor visul, or a visual that is 'preferred' over the
             * previous 'best' visual.
             */
            if (bestvisual_depth == -1
                || (visuals[i].depth >= 15
                    && (visuals[i].depth < bestvisual_depth
                        || bestvisual_depth < 15)))
            {
                bestvisual = i;
                bestvisual_depth = visuals[i].depth;
            }
        }

        if (bestvisual != -1 && visual_return != NULL)
            *visual_return = visuals[bestvisual].visual;

        XFree(visuals);
    }
    return bestvisual_depth;
}


static Colormap cmap = None;
static XColor cols[256];
static int cm_size, red_mask, green_mask, blue_mask;


Colormap vo_x11s_create_colormap_S(tScreenOutput *Screen)
{
    unsigned k, r, g, b, ru, gu, bu, m, rv, gv, bv, rvu, gvu, bvu;

    if (Screen->vinfo.class != DirectColor)
        return XCreateColormap(Screen->display, Screen->parent, Screen->vinfo.visual,
                               AllocNone);

    /* can this function get called twice or more? */
    if (cmap)
        return cmap;
    cm_size = Screen->vinfo.colormap_size;
    red_mask = Screen->vinfo.red_mask;
    green_mask = Screen->vinfo.green_mask;
    blue_mask = Screen->vinfo.blue_mask;
    ru = (red_mask & (red_mask - 1)) ^ red_mask;
    gu = (green_mask & (green_mask - 1)) ^ green_mask;
    bu = (blue_mask & (blue_mask - 1)) ^ blue_mask;
    rvu = 65536ull * ru / (red_mask + ru);
    gvu = 65536ull * gu / (green_mask + gu);
    bvu = 65536ull * bu / (blue_mask + bu);
    r = g = b = 0;
    rv = gv = bv = 0;
    m = DoRed | DoGreen | DoBlue;
    for (k = 0; k < cm_size; k++)
    {
        int t;

        cols[k].pixel = r | g | b;
        cols[k].red = rv;
        cols[k].green = gv;
        cols[k].blue = bv;
        cols[k].flags = m;
        t = (r + ru) & red_mask;
        if (t < r)
            m &= ~DoRed;
        r = t;
        t = (g + gu) & green_mask;
        if (t < g)
            m &= ~DoGreen;
        g = t;
        t = (b + bu) & blue_mask;
        if (t < b)
            m &= ~DoBlue;
        b = t;
        rv += rvu;
        gv += gvu;
        bv += bvu;
    }
    cmap = XCreateColormap(Screen->display, Screen->parent, Screen->vinfo.visual, AllocAll);
    XStoreColors(Screen->display, cmap, cols, cm_size);
    return cmap;
}

/*
 * Via colormaps/gamma ramps we can do gamma, brightness, contrast,
 * hue and red/green/blue intensity, but we cannot do saturation.
 * Currently only gamma, brightness and contrast are implemented.
 * Is there sufficient interest for hue and/or red/green/blue intensity?
 */
/* these values have range [-100,100] and are initially 0 */
static int vo_gamma = 0;
static int vo_brightness = 0;
static int vo_contrast = 0;

static int transform_color(float val,
                           float brightness, float contrast, float gamma) {
    float s = pow(val, gamma);
    s = (s - 0.5) * contrast + 0.5;
    s += brightness;
    if (s < 0)
        s = 0;
    if (s > 1)
        s = 1;
    return (unsigned short) (s * 65535);
}

uint32_t vo_x11s_set_equalizer(char *name, int value)
{
    float gamma, brightness, contrast;
    float rf, gf, bf;
    int k;

    /*
     * IMPLEMENTME: consider using XF86VidModeSetGammaRamp in the case
     * of TrueColor-ed window but be careful:
     * Unlike the colormaps, which are private for the X client
     * who created them and thus automatically destroyed on client
     * disconnect, this gamma ramp is a system-wide (X-server-wide)
     * setting and _must_ be restored before the process exits.
     * Unforunately when the process crashes (or gets killed
     * for some reason) it is impossible to restore the setting,
     * and such behaviour could be rather annoying for the users.
     */
    if (cmap == None)
        return VO_NOTAVAIL;

    if (!strcasecmp(name, "brightness"))
        vo_brightness = value;
    else if (!strcasecmp(name, "contrast"))
        vo_contrast = value;
    else if (!strcasecmp(name, "gamma"))
        vo_gamma = value;
    else
        return VO_NOTIMPL;

    brightness = 0.01 * vo_brightness;
    contrast = tan(0.0095 * (vo_contrast + 100) * M_PI / 4);
    gamma = pow(2, -0.02 * vo_gamma);

    rf = (float) ((red_mask & (red_mask - 1)) ^ red_mask) / red_mask;
    gf = (float) ((green_mask & (green_mask - 1)) ^ green_mask) /
        green_mask;
    bf = (float) ((blue_mask & (blue_mask - 1)) ^ blue_mask) / blue_mask;

    /* now recalculate the colormap using the newly set value */
    for (k = 0; k < cm_size; k++)
    {
        cols[k].red   = transform_color(rf * k, brightness, contrast, gamma);
        cols[k].green = transform_color(gf * k, brightness, contrast, gamma);
        cols[k].blue  = transform_color(bf * k, brightness, contrast, gamma);
    }

    XStoreColors(ScreenLeft.display, cmap, cols, cm_size);
    XFlush(ScreenLeft.display);
    XStoreColors(ScreenRight.display, cmap, cols, cm_size);
    XFlush(ScreenRight.display);
    return VO_TRUE;
}

uint32_t vo_x11s_get_equalizer(char *name, int *value)
{
    if (cmap == None)
        return VO_NOTAVAIL;
    if (!strcasecmp(name, "brightness"))
        *value = vo_brightness;
    else if (!strcasecmp(name, "contrast"))
        *value = vo_contrast;
    else if (!strcasecmp(name, "gamma"))
        *value = vo_gamma;
    else
        return VO_NOTIMPL;
    return VO_TRUE;
}

#ifdef CONFIG_XV
int vo_xvs_set_eq(tScreenOutput *Screen, char *name, int value)
{
    XvAttribute *attributes;
    int i, howmany, xv_atom;

    mp_dbg(MSGT_VO, MSGL_V, "xv_set_eq called! (%s, %d)\n", name, value);

    /* get available attributes */
    attributes = XvQueryPortAttributes(Screen->display, Screen->xv_port, &howmany);
    for (i = 0; i < howmany && attributes; i++)
        if (attributes[i].flags & XvSettable)
        {
            xv_atom = XInternAtom(Screen->display, attributes[i].name, True);
/* since we have SET_DEFAULTS first in our list, we can check if it's available
   then trigger it if it's ok so that the other values are at default upon query */
            if (xv_atom != None)
            {
                int hue = 0, port_value, port_min, port_max;

                if (!strcmp(attributes[i].name, "XV_BRIGHTNESS") &&
                    (!strcasecmp(name, "brightness")))
                    port_value = value;
                else if (!strcmp(attributes[i].name, "XV_CONTRAST") &&
                         (!strcasecmp(name, "contrast")))
                    port_value = value;
                else if (!strcmp(attributes[i].name, "XV_SATURATION") &&
                         (!strcasecmp(name, "saturation")))
                    port_value = value;
                else if (!strcmp(attributes[i].name, "XV_HUE") &&
                         (!strcasecmp(name, "hue")))
                {
                    port_value = value;
                    hue = 1;
                } else
                    /* Note: since 22.01.2002 GATOS supports these attrs for radeons (NK) */
                if (!strcmp(attributes[i].name, "XV_RED_INTENSITY") &&
                        (!strcasecmp(name, "red_intensity")))
                    port_value = value;
                else if (!strcmp(attributes[i].name, "XV_GREEN_INTENSITY")
                         && (!strcasecmp(name, "green_intensity")))
                    port_value = value;
                else if (!strcmp(attributes[i].name, "XV_BLUE_INTENSITY")
                         && (!strcasecmp(name, "blue_intensity")))
                    port_value = value;
                else
                    continue;

                port_min = attributes[i].min_value;
                port_max = attributes[i].max_value;

                /* nvidia hue workaround */
                if (hue && port_min == 0 && port_max == 360)
                {
                    port_value =
                        (port_value >=
                         0) ? (port_value - 100) : (port_value + 100);
                }
                // -100 -> min
                //   0  -> (max+min)/2
                // +100 -> max
                port_value =
                    (port_value + 100) * (port_max - port_min) / 200 +
                    port_min;
                XvSetPortAttribute(Screen->display, Screen->xv_port, xv_atom, port_value);
                return VO_TRUE;
            }
        }
    return VO_FALSE;
}

int vo_xvs_get_eq(tScreenOutput *Screen, char *name, int *value)
{

    XvAttribute *attributes;
    int i, howmany, xv_atom;

    /* get available attributes */
    attributes = XvQueryPortAttributes(Screen->display, Screen->xv_port, &howmany);
    for (i = 0; i < howmany && attributes; i++)
        if (attributes[i].flags & XvGettable)
        {
            xv_atom = XInternAtom(Screen->display, attributes[i].name, True);
/* since we have SET_DEFAULTS first in our list, we can check if it's available
   then trigger it if it's ok so that the other values are at default upon query */
            if (xv_atom != None)
            {
                int val, port_value = 0, port_min, port_max;

                XvGetPortAttribute(Screen->display, Screen->xv_port, xv_atom,
                                   &port_value);

                port_min = attributes[i].min_value;
                port_max = attributes[i].max_value;
                val =
                    (port_value - port_min) * 200 / (port_max - port_min) -
                    100;

                if (!strcmp(attributes[i].name, "XV_BRIGHTNESS") &&
                    (!strcasecmp(name, "brightness")))
                    *value = val;
                else if (!strcmp(attributes[i].name, "XV_CONTRAST") &&
                         (!strcasecmp(name, "contrast")))
                    *value = val;
                else if (!strcmp(attributes[i].name, "XV_SATURATION") &&
                         (!strcasecmp(name, "saturation")))
                    *value = val;
                else if (!strcmp(attributes[i].name, "XV_HUE") &&
                         (!strcasecmp(name, "hue")))
                {
                    /* nasty nvidia detect */
                    if (port_min == 0 && port_max == 360)
                        *value = (val >= 0) ? (val - 100) : (val + 100);
                    else
                        *value = val;
                } else
                    /* Note: since 22.01.2002 GATOS supports these attrs for radeons (NK) */
                if (!strcmp(attributes[i].name, "XV_RED_INTENSITY") &&
                        (!strcasecmp(name, "red_intensity")))
                    *value = val;
                else if (!strcmp(attributes[i].name, "XV_GREEN_INTENSITY")
                         && (!strcasecmp(name, "green_intensity")))
                    *value = val;
                else if (!strcmp(attributes[i].name, "XV_BLUE_INTENSITY")
                         && (!strcasecmp(name, "blue_intensity")))
                    *value = val;
                else
                    continue;

                mp_dbg(MSGT_VO, MSGL_V, "xv_get_eq called! (%s, %d)\n",
                       name, *value);
                return VO_TRUE;
            }
        }
    return VO_FALSE;
}

/** \brief contains flags changing the execution of the colorkeying code */
xvs_ck_info_t xvs_ck_info = { CK_METHOD_MANUALFILL, CK_SRC_CUR };
unsigned long xvs_colorkey; ///< The color used for manual colorkeying.

/**
 * \brief Interns the requested atom if it is available.
 *
 * \param atom_name String containing the name of the requested atom.
 *
 * \return Returns the atom if available, else None is returned.
 *
 */
static Atom xv_intern_atom_if_exists(tScreenOutput *Screen, char const * atom_name )
{
  XvAttribute * attributes;
  int attrib_count,i;
  Atom xv_atom = None;

  attributes = XvQueryPortAttributes( Screen->display, Screen->xv_port, &attrib_count );
  if( attributes!=NULL )
  {
    for ( i = 0; i < attrib_count; ++i )
    {
      if ( strcmp(attributes[i].name, atom_name ) == 0 )
      {
        xv_atom = XInternAtom( Screen->display, atom_name, False );
        break; // found what we want, break out
      }
    }
    XFree( attributes );
  }

  return xv_atom;
}

/**
 * \brief Try to enable vsync for xv.
 * \return Returns -1 if not available, 0 on failure and 1 on success.
 */
int vo_xvs_enable_vsync(tScreenOutput *Screen)
{
  Atom xv_atom = xv_intern_atom_if_exists(Screen, "XV_SYNC_TO_VBLANK");
  if (xv_atom == None)
    return -1;
  return XvSetPortAttribute(Screen->display, Screen->xv_port, xv_atom, 1) == Success;
}

/**
 * \brief Get maximum supported source image dimensions.
 *
 *   This function does not set the variables pointed to by
 * width and height if the information could not be retrieved,
 * so the caller is reponsible for properly initializing them.
 *
 * \param width [out] The maximum width gets stored here.
 * \param height [out] The maximum height gets stored here.
 *
 */
void vo_xvs_get_max_img_dim(tScreenOutput *Screen, uint32_t * width, uint32_t * height )
{
  XvEncodingInfo * encodings;
  //unsigned long num_encodings, idx; to int or too long?!
  unsigned int num_encodings, idx;

  XvQueryEncodings( Screen->display, Screen->xv_port, &num_encodings, &encodings);

  if ( encodings )
  {
      for ( idx = 0; idx < num_encodings; ++idx )
      {
          if ( strcmp( encodings[idx].name, "XV_IMAGE" ) == 0 )
          {
              *width  = encodings[idx].width;
              *height = encodings[idx].height;
              break;
          }
      }
  }

  mp_msg( MSGT_VO, MSGL_V,
          "[xv common] Maximum source image dimensions: %ux%u\n",
          *width, *height );

  XvFreeEncodingInfo( encodings );
}

/**
 * \brief Print information about the colorkey method and source.
 *
 * \param ck_handling Integer value containing the information about
 *                    colorkey handling (see x11_common.h).
 *
 * Outputs the content of |ck_handling| as a readable message.
 *
 */
void vo_xvs_print_ck_info(tScreenOutput *Screen)
{
  mp_msg( MSGT_VO, MSGL_V, "[xv common] " );

  switch ( xvs_ck_info.method )
  {
    case CK_METHOD_NONE:
      mp_msg( MSGT_VO, MSGL_V, "Drawing no colorkey.\n" ); return;
    case CK_METHOD_AUTOPAINT:
      mp_msg( MSGT_VO, MSGL_V, "Colorkey is drawn by Xv." ); break;
    case CK_METHOD_MANUALFILL:
      mp_msg( MSGT_VO, MSGL_V, "Drawing colorkey manually." ); break;
    case CK_METHOD_BACKGROUND:
      mp_msg( MSGT_VO, MSGL_V, "Colorkey is drawn as window background." ); break;
  }

  mp_msg( MSGT_VO, MSGL_V, "\n[xv common] " );

  switch ( xvs_ck_info.source )
  {
    case CK_SRC_CUR:
      mp_msg( MSGT_VO, MSGL_V, "Using colorkey from Xv (0x%06lx).\n",
              Screen->xv_colorkey );
      break;
    case CK_SRC_USE:
      if ( xvs_ck_info.method == CK_METHOD_AUTOPAINT )
      {
        mp_msg( MSGT_VO, MSGL_V,
                "Ignoring colorkey from MPlayer (0x%06lx).\n",
                Screen->xv_colorkey );
      }
      else
      {
        mp_msg( MSGT_VO, MSGL_V,
                "Using colorkey from MPlayer (0x%06lx)."
                " Use -colorkey to change.\n",
                Screen->xv_colorkey );
      }
      break;
    case CK_SRC_SET:
      mp_msg( MSGT_VO, MSGL_V,
              "Setting and using colorkey from MPlayer (0x%06lx)."
              " Use -colorkey to change.\n",
              Screen->xv_colorkey );
      break;
  }
}
/**
 * \brief Init colorkey depending on the settings in xvs_ck_info.
 *
 * \return Returns 0 on failure and 1 on success.
 *
 * Sets the colorkey variable according to the CK_SRC_* and CK_METHOD_*
 * flags in xvs_ck_info.
 *
 * Possiblilities:
 *   * Methods
 *     - manual colorkey drawing ( CK_METHOD_MANUALFILL )
 *     - set colorkey as window background ( CK_METHOD_BACKGROUND )
 *     - let Xv paint the colorkey ( CK_METHOD_AUTOPAINT )
 *   * Sources
 *     - use currently set colorkey ( CK_SRC_CUR )
 *     - use colorkey in vo_colorkey ( CK_SRC_USE )
 *     - use and set colorkey in vo_colorkey ( CK_SRC_SET )
 *
 * NOTE: If vo_colorkey has bits set after the first 3 low order bytes
 *       we don't draw anything as this means it was forced to off.
 */
int vo_xvs_init_colorkey(tScreenOutput *Screen)
{
  Atom xv_atom;
  int rez;

  /* check if colorkeying is needed */
  xv_atom = xv_intern_atom_if_exists(Screen, "XV_COLORKEY" );

  /* if we have to deal with colorkeying ... */
  if( xv_atom != None && !(vo_colorkey & 0xFF000000) )
  {
    /* check if we should use the colorkey specified in vo_colorkey */
    if ( xvs_ck_info.source != CK_SRC_CUR )
    {
      Screen->xv_colorkey = vo_colorkey;

      /* check if we have to set the colorkey too */
      if ( xvs_ck_info.source == CK_SRC_SET )
      {
        xv_atom = XInternAtom(Screen->display, "XV_COLORKEY",False);

        rez = XvSetPortAttribute(Screen->display, Screen->xv_port, xv_atom, vo_colorkey );
        if ( rez != Success )
        {
          mp_msg( MSGT_VO, MSGL_FATAL,
                  "[xv common] Couldn't set colorkey!\n" );
          return 0; // error setting colorkey
        }
      }
    }
    else
    {
      int colorkey_ret;

      rez=XvGetPortAttribute(Screen->display, Screen->xv_port, xv_atom, &colorkey_ret);
      if ( rez == Success )
      {
         Screen->xv_colorkey = colorkey_ret;
      }
      else
      {
        mp_msg( MSGT_VO, MSGL_FATAL,
                "[xv common] Couldn't get colorkey!"
                "Maybe the selected Xv port has no overlay.\n" );
        return 0; // error getting colorkey
      }
    }

    xv_atom = xv_intern_atom_if_exists(Screen, "XV_AUTOPAINT_COLORKEY" );

    /* should we draw the colorkey ourselves or activate autopainting? */
    if ( xvs_ck_info.method == CK_METHOD_AUTOPAINT )
    {
      rez = !Success; // reset rez to something different than Success

      if ( xv_atom != None ) // autopaint is supported
      {
        rez = XvSetPortAttribute( Screen->display, Screen->xv_port, xv_atom, 1 );
      }

      if ( rez != Success )
      {
        // fallback to manual colorkey drawing
        xvs_ck_info.method = CK_METHOD_MANUALFILL;
      }
    }
    else // disable colorkey autopainting if supported
    {
      if ( xv_atom != None ) // we have autopaint attribute
      {
        XvSetPortAttribute( Screen->display, Screen->xv_port, xv_atom, 0 );
      }
    }
  }
  else // do no colorkey drawing at all
  {
    xvs_ck_info.method = CK_METHOD_NONE;
  } /* end: should we draw colorkey */

  /* output information about the current colorkey settings */
  vo_xvs_print_ck_info(Screen);

  return 1; // success
}

/**
 * \brief Draw the colorkey on the video window.
 *
 * Draws the colorkey depending on the set method ( colorkey_handling ).
 *
 * Also draws the black bars ( when the video doesn't fit the display in
 * fullscreen ) separately, so they don't overlap with the video area.
 * It doesn't call XFlush.
 *
 */
static
void vo_xvs_draw_colorkey_S(  int32_t x,  int32_t y,
			int32_t w,  int32_t h, tScreenOutput *Screen);

void vo_xvs_draw_colorkey(  int32_t x,  int32_t y,
                                  int32_t w,  int32_t h  )
{
	vo_xvs_draw_colorkey_S(x, y, w, h, &ScreenLeft);
	vo_xvs_draw_colorkey_S(x, y, w, h, &ScreenRight);
}

static
void vo_xvs_draw_colorkey_S(  int32_t x,  int32_t y,
			int32_t w,  int32_t h, tScreenOutput *Screen)
{
  if( xvs_ck_info.method == CK_METHOD_MANUALFILL ||
      xvs_ck_info.method == CK_METHOD_BACKGROUND   )//less tearing than XClearWindow()
  {
    XSetForeground( Screen->display, Screen->vo_gc, Screen->xv_colorkey );
    XFillRectangle( Screen->display, Screen->window, Screen->vo_gc,
                    x, y,
                    w, h );
  }

  /* draw black bars if needed */
  /* TODO! move this to vo_x11s_clearwindow_part() */
  if ( vo_fs )
  {
    XSetForeground( Screen->display, Screen->vo_gc, 0 );
    /* making non-overlap fills, requires 8 checks instead of 4 */
    if ( y > 0 )
      XFillRectangle( Screen->display, Screen->window, Screen->vo_gc,
                      0, 0,
                      Screen->vo_screenwidth, y);
    if (x > 0)
      XFillRectangle( Screen->display, Screen->window, Screen->vo_gc,
                      0, 0,
                      x, Screen->vo_screenheight);
    if (x + w < Screen->vo_screenwidth)
      XFillRectangle( Screen->display, Screen->window, Screen->vo_gc,
                      x + w, 0,
                      Screen->vo_screenwidth, Screen->vo_screenheight);
    if (y + h < Screen->vo_screenheight)
      XFillRectangle( Screen->display, Screen->window, Screen->vo_gc,
                      0, y + h,
                      Screen->vo_screenwidth, Screen->vo_screenheight);
  }
}

/** \brief Tests if a valid argument for the ck suboption was given. */
int xvs_test_ck( void * arg )
{
  strarg_t * strarg = (strarg_t *)arg;

  if ( strargcmp( strarg, "use" ) == 0 ||
       strargcmp( strarg, "set" ) == 0 ||
       strargcmp( strarg, "cur" ) == 0    )
  {
    return 1;
  }

  return 0;
}
/** \brief Tests if a valid arguments for the ck-method suboption was given. */
int xvs_test_ckm( void * arg )
{
  strarg_t * strarg = (strarg_t *)arg;

  if ( strargcmp( strarg, "bg" ) == 0 ||
       strargcmp( strarg, "man" ) == 0 ||
       strargcmp( strarg, "auto" ) == 0    )
  {
    return 1;
  }

  return 0;
}

/**
 * \brief Modify the colorkey_handling var according to str
 *
 * Checks if a valid pointer ( not NULL ) to the string
 * was given. And in that case modifies the colorkey_handling
 * var to reflect the requested behaviour.
 * If nothing happens the content of colorkey_handling stays
 * the same.
 *
 * \param str Pointer to the string or NULL
 *
 */
void xvs_setup_colorkeyhandling( char const * ck_method_str,
                                char const * ck_str )
{
  /* check if a valid pointer to the string was passed */
  if ( ck_str )
  {
    if ( strncmp( ck_str, "use", 3 ) == 0 )
    {
      xvs_ck_info.source = CK_SRC_USE;
    }
    else if ( strncmp( ck_str, "set", 3 ) == 0 )
    {
      xvs_ck_info.source = CK_SRC_SET;
    }
  }
  /* check if a valid pointer to the string was passed */
  if ( ck_method_str )
  {
    if ( strncmp( ck_method_str, "bg", 2 ) == 0 )
    {
      xvs_ck_info.method = CK_METHOD_BACKGROUND;
    }
    else if ( strncmp( ck_method_str, "man", 3 ) == 0 )
    {
      xvs_ck_info.method = CK_METHOD_MANUALFILL;
    }
    else if ( strncmp( ck_method_str, "auto", 4 ) == 0 )
    {
      xvs_ck_info.method = CK_METHOD_AUTOPAINT;
    }
  }
}

#endif

void initScreen(tScreenOutput *Screen, tSide side) {
	Screen->displayName = NULL;
	Screen->display = NULL;
	Screen->window = 0;
	Screen->parent = 0;
	Screen->screen = 0;
	Screen->mLocalDisplay = 0;
	memset(&Screen->vinfo,0,sizeof(XVisualInfo));
	Screen->vo_wintitle = NULL;
	Screen->vo_gc = NULL;
	Screen->f_gc = NULL;
	memset(&Screen->vo_hint,0,sizeof(XSizeHints));
	Screen->vo_bpp = 0;
	Screen->vo_screenwidth = 0;
	Screen->vo_screenheight = 0;
	Screen->vo_old_x = 0;
	Screen->vo_old_y = 0;
	Screen->vo_old_width = 0;
	Screen->vo_old_height = 0;
	Screen->side = side;
#ifdef CONFIG_XV
	Screen->xv_port = 0;
	Screen->xv_ck_info.method = 0;
	Screen->xv_ck_info.source = 0;
	Screen->xv_colorkey = 0;
#endif
};

