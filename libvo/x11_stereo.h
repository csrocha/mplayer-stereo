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

#ifndef MPLAYER_X11_COMMON_H
#define MPLAYER_X11_COMMON_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "config.h"

#if defined(CONFIG_GL) || defined(CONFIG_X11) || defined(CONFIG_XV)
#define X11_FULLSCREEN 1
#endif

#ifdef X11_FULLSCREEN

#define vo_wm_LAYER 1
#define vo_wm_FULLSCREEN 2
#define vo_wm_STAYS_ON_TOP 4
#define vo_wm_ABOVE 8
#define vo_wm_BELOW 16
#define vo_wm_NETWM (vo_wm_FULLSCREEN | vo_wm_STAYS_ON_TOP | vo_wm_ABOVE | vo_wm_BELOW)

/* EWMH state actions, see
	 http://freedesktop.org/Standards/wm-spec/index.html#id2768769 */
#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

extern int metacity_hack;

extern int vo_fs_layer;
extern int vo_wm_type;
extern int vo_fs_type;
extern char** vo_fstype_list;

#ifdef CONFIG_XV
/*** colorkey handling ***/
typedef struct xvs_ck_info_s
{
  int method; ///< CK_METHOD_* constants
  int source; ///< CK_SRC_* constants
} xvs_ck_info_t;
#endif

typedef enum { RIGHT, LEFT } tSide;
extern int uniqueDisplay;

struct sScreenOutput {
	char *displayName;
	Display *display;
	Window parent;
	int screen;
	int mLocalDisplay;
	XVisualInfo vinfo;
        Window     window;
	char *vo_wintitle;
	GC vo_gc;
	GC f_gc;
	XSizeHints vo_hint;
	// correct resolution/bpp on screen: (should be autodetected by vo_s_init
	int vo_bpp;
	int vo_screenwidth;
	int vo_screenheight;
 	// window position
	int vo_old_x;
	int vo_old_y;
	int vo_old_width;
	int vo_old_height;
	tSide side;
#ifdef CONFIG_XV
	unsigned int xv_port;
	xvs_ck_info_t xv_ck_info;
	unsigned long xv_colorkey;
#endif
};

typedef struct sScreenOutput tScreenOutput;

void initScreen(tScreenOutput *Screen, tSide side);

extern tScreenOutput ScreenLeft;
extern tScreenOutput ScreenRight;

extern int mLocalDisplay;

extern int vo_mouse_autohide;

int vo_s_init(void);
int vo_init_S( const char *DISPLAY, tScreenOutput *Screen );
void vo_uninit_S( tScreenOutput *Screen );
void vo_hidecursor_S ( tScreenOutput *Screen );
void vo_showcursor_S ( tScreenOutput *Screen );
void vo_x11s_decoration_S ( tScreenOutput *Screen, int d );
void vo_x11s_classhint_S ( tScreenOutput *Screen,const char *name );
void vo_x11s_nofs_sizepos_S( tScreenOutput *Screen, int x, int y, int width, int height);
void vo_x11s_sizehint_S(tScreenOutput *Screen, int x, int y, int width, int height, int max);
int vo_x11s_check_events_S( tScreenOutput *Screen );
void vo_x11s_selectinput_witherr_S( tScreenOutput *Screen, long event_mask);
int vo_x11s_update_geometry_S( tScreenOutput *Screen );
void vo_x11s_fullscreen(void);
void vo_x11s_fullscreen_S( tScreenOutput *Screen, int vo_fs );
void vo_x11s_setlayer_S ( tScreenOutput *Screen, int layer );
void vo_x11s_uninit(void);
void vo_x11s_uninit_S ( tScreenOutput *Screen );
Colormap vo_x11s_create_colormap_S( tScreenOutput *Screen );
uint32_t vo_x11s_set_equalizer(char *name, int value);
uint32_t vo_x11s_get_equalizer(char *name, int *value);
#ifndef CONFIG_X11
void fstype_help(void);
#endif
Window vo_x11s_create_smooth_window_S( tScreenOutput *Screen,
	Visual *vis, int x, int y, unsigned int width, unsigned int height,
	int depth, Colormap col_map);
void vo_x11s_create_vo_window_S( tScreenOutput *Screen, int x, int y,
	unsigned int width, unsigned int height, int flags,
	Colormap col_map, const char *classname, const char *title);
void vo_x11s_clearwindow_part_S( tScreenOutput *Screen,
	int img_width, int img_height, int use_fs);
void vo_x11s_clearwindow_S( tScreenOutput *Screen );
void vo_x11s_ontop(void);
void vo_x11s_border(void);
void vo_x11s_ewmh_fullscreen_S(tScreenOutput *Screen, int action);
void update_xinerama_info_S(tScreenOutput *Screen);

#endif

#ifdef CONFIG_XV
//XvPortID xv_port;

int vo_xvs_set_eq( tScreenOutput *Screen, char * name, int value);
int vo_xvs_get_eq( tScreenOutput *Screen, char * name, int *value);

int vo_xvs_enable_vsync(tScreenOutput *Screen);
void vo_xvs_print_ck_info(tScreenOutput *Screen);

void vo_xvs_get_max_img_dim( tScreenOutput *Screen, uint32_t * width, uint32_t * height );

#define CK_METHOD_NONE       0 ///< no colorkey drawing
#define CK_METHOD_BACKGROUND 1 ///< set colorkey as window background
#define CK_METHOD_AUTOPAINT  2 ///< let xv draw the colorkey
#define CK_METHOD_MANUALFILL 3 ///< manually draw the colorkey
#define CK_SRC_USE           0 ///< use specified / default colorkey
#define CK_SRC_SET           1 ///< use and set specified / default colorkey
#define CK_SRC_CUR           2 ///< use current colorkey ( get it from xv )

int vo_xvs_init_colorkey( tScreenOutput *Screen );
void vo_xvs_draw_colorkey( int32_t x, int32_t y, int32_t w, int32_t h);
void xvs_setup_colorkeyhandling( char const * ck_method_str, char const * ck_str);

/*** test functions for common suboptions ***/
int xvs_test_ck( void * arg );
int xvs_test_ckm( void * arg );
#endif

void vo_setwindow_S(tScreenOutput *Screen, Window w,GC g );
void vo_x11s_putkey(int key);

void xscreensaver_heartbeat_S( tScreenOutput *Screen );
void saver_off_S( tScreenOutput *Screen );
void saver_on_S( tScreenOutput *Screen );

#ifdef CONFIG_XF86VM
void vo_vms_switch(void);
void vo_vms_close(void);
#endif


int vo_find_depth_from_visuals_S(tScreenOutput *Screen, Visual **visual_return);

#endif /* MPLAYER_X11_COMMON_H */
