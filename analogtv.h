/* analogtv, Copyright (c) 2003, 2004 Trevor Blackwell <tlb@tlb.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

#ifndef _XSCREENSAVER_ANALOGTV_H
#define _XSCREENSAVER_ANALOGTV_H

#include "thread_util.h"
#ifdef X11
#ifdef HAVE_XSHM_EXTENSION
#include "xshm.h"
#endif
#elif defined ALLEGRO
# include <allegro.h>
#endif

#ifdef WIN32
#include <Windows.h>
#endif
#include "xssemu.h"

/*
  You'll need these to generate standard NTSC TV signals
 */
enum {
  /* We don't handle interlace here */
  ANALOGTV_V=262,
  ANALOGTV_TOP=30,
  ANALOGTV_VISLINES=200,
  ANALOGTV_BOT=ANALOGTV_TOP + ANALOGTV_VISLINES,

  /* This really defines our sampling rate, 4x the colorburst
     frequency. Handily equal to the Apple II's dot clock.
     You could also make a case for using 3x the colorburst freq,
     but 4x isn't hard to deal with. */
  ANALOGTV_H=912,

  /* Each line is 63500 nS long. The sync pulse is 4700 nS long, etc.
     Define sync, back porch, colorburst, picture, and front porch
     positions */
  ANALOGTV_SYNC_START=0,
  ANALOGTV_BP_START=4700*ANALOGTV_H/63500,
  ANALOGTV_CB_START=5800*ANALOGTV_H/63500,
  /* signal[row][ANALOGTV_PIC_START] is the first displayed pixel */
  ANALOGTV_PIC_START=9400*ANALOGTV_H/63500,
  ANALOGTV_PIC_LEN=52600*ANALOGTV_H/63500,
  ANALOGTV_FP_START=62000*ANALOGTV_H/63500,
  ANALOGTV_PIC_END=ANALOGTV_FP_START,

  /* TVs scan past the edges of the picture tube, so normally you only
     want to use about the middle 3/4 of the nominal scan line.
  */
  ANALOGTV_VIS_START=ANALOGTV_PIC_START + (ANALOGTV_PIC_LEN*1/8),
  ANALOGTV_VIS_END=ANALOGTV_PIC_START + (ANALOGTV_PIC_LEN*7/8),
  ANALOGTV_VIS_LEN=ANALOGTV_VIS_END-ANALOGTV_VIS_START,

  ANALOGTV_HASHNOISE_LEN=6,

  ANALOGTV_GHOSTFIR_LEN=4,

  /* analogtv.signal is in IRE units, as defined below: */
  ANALOGTV_WHITE_LEVEL=100,
  ANALOGTV_GRAY50_LEVEL=55,
  ANALOGTV_GRAY30_LEVEL=35,
  ANALOGTV_BLACK_LEVEL=10,
  ANALOGTV_BLANK_LEVEL=0,
  ANALOGTV_SYNC_LEVEL=-40,
  ANALOGTV_CB_LEVEL=20,

  ANALOGTV_SIGNAL_LEN=ANALOGTV_V*ANALOGTV_H,

  /* The number of intensity levels we deal with for gamma correction &c */
  ANALOGTV_CV_MAX=1024,

  /* MAX_LINEHEIGHT corresponds to 2400 vertical pixels, beyond which
     it interpolates extra black lines. */
  ANALOGTV_MAX_LINEHEIGHT=12

};

enum {
  ANALOGTV_COLOR_BLACK,
  ANALOGTV_COLOR_WHITE,
  ANALOGTV_COLOR_RED,
  ANALOGTV_COLOR_PALE_GREEN,
  ANALOGTV_COLOR_MAGENTA,
  ANALOGTV_COLOR_BRIGHT_GREEN,
  ANALOGTV_COLOR_DARK_BLUE,
  ANALOGTV_COLOR_YELLOW,
  ANALOGTV_COLOR_SALMON,
  ANALOGTV_COLOR_BROWN,
  ANALOGTV_COLOR_HOT_PINK,
  ANALOGTV_COLOR_DARKER_GRAY,
  ANALOGTV_COLOR_DARK_GRAY,
  ANALOGTV_COLOR_YELLOW_GREEN,
  ANALOGTV_COLOR_CYAN,
  ANALOGTV_COLOR_GRAY
};

typedef struct analogtv_input_s {
  signed char signal[ANALOGTV_V+1][ANALOGTV_H];

  int do_teletext;

  /* for client use */
  void (*updater)(struct analogtv_input_s *inp);
  void *client_data;
  double next_update_time;

} analogtv_input;

typedef struct analogtv_font_s {
#ifdef WIN32
	HBITMAP text_im;
#elif defined X11
  XImage *text_im;
#elif defined ALLEGRO
  BITMAP *text_im;
#endif
  int char_w, char_h;
  int x_mult, y_mult;
} analogtv_font;

typedef struct analogtv_reception_s {

  analogtv_input *input;
  double ofs;
  double level;
  double multipath;
  double freqerr;

  double ghostfir[ANALOGTV_GHOSTFIR_LEN];
  double ghostfir2[ANALOGTV_GHOSTFIR_LEN];

  double hfloss;
  double hfloss2;

} analogtv_reception;

/*
  The rest of this should be considered mostly opaque to the analogtv module.
 */

struct analogtv_yiq_s {
  float y,i,q;
} /*yiq[ANALOGTV_PIC_LEN+10] */;

typedef struct analogtv_s {

#ifdef WIN32
  HWND window;
#elif defined X11
  Display *dpy;
  Window window;
  Screen *screen;
  XWindowAttributes xgwa;
#elif defined ALLEGRO
  BITMAP *dpy; /* Just a copy of screen. */
#endif

  struct threadpool threads;

#if 0
  unsigned int onscreen_signature[ANALOGTV_V];
#endif

  int n_colors;

  int interlace;
  int interlace_counter;

  float agclevel;

  /* If you change these, call analogtv_set_demod */
  float tint_control,color_control,brightness_control,contrast_control;
  float height_control, width_control, squish_control;
  float horiz_desync;
  float squeezebottom;
  float powerup;

  /* internal cache */
  int blur_mult;

  /* For fast display, set fakeit_top, fakeit_bot to
     the scanlines (0..ANALOGTV_V) that can be preserved on screen.
     fakeit_scroll is the number of scan lines to scroll it up,
     or 0 to not scroll at all. It will DTRT if asked to scroll from
     an offscreen region.
  */
  int fakeit_top;
  int fakeit_bot;
  int fakeit_scroll;
  int redraw_all;

  int use_shm,use_cmap,use_color;
  int bilevel_signal;

#ifdef X11
#ifdef HAVE_XSHM_EXTENSION
  XShmSegmentInfo shm_info;
#endif
  int visdepth,visclass,visbits;
#endif
  int red_invprec, red_shift;
  int green_invprec, green_shift;
  int blue_invprec, blue_shift;
  unsigned int red_mask, green_mask, blue_mask;

#ifdef X11
  Colormap colormap;
#endif
  int usewidth,useheight,xrepl,subwidth;
#ifdef X11
  XImage *image; /* usewidth * useheight */
  GC gc;
#elif defined WIN32
  HBITMAP image;
  //int image_size;
#elif defined ALLEGRO
  BITMAP* image;
#endif
  int screen_xo,screen_yo; /* centers image in window */

  int flutter_horiz_desync;
  int flutter_tint;

  struct timeval last_display_time;
  int need_clear;


  /* Add hash (in the radio sense, not the programming sense.) These
     are the small white streaks that appear in quasi-regular patterns
     all over the screen when someone is running the vacuum cleaner or
     the blender. We also set shrinkpulse for one period which
     squishes the image horizontally to simulate the temporary line
     voltate drop when someone turns on a big motor */
  double hashnoise_rpm;
  int hashnoise_counter;
  int hashnoise_times[ANALOGTV_V];
  int hashnoise_signal[ANALOGTV_V];
  int hashnoise_on;
  int hashnoise_enable;
  int shrinkpulse;

  float crtload[ANALOGTV_V];

  unsigned int red_values[ANALOGTV_CV_MAX];
  unsigned int green_values[ANALOGTV_CV_MAX];
  unsigned int blue_values[ANALOGTV_CV_MAX];

  unsigned long colors[256];
  int cmap_y_levels;
  int cmap_i_levels;
  int cmap_q_levels;

  float tint_i, tint_q;
  
  int cur_hsync;
  int line_hsync[ANALOGTV_V];
  int cur_vsync;
  double cb_phase[4];
  double line_cb_phase[ANALOGTV_V][4];

  int channel_change_cycles;
  double rx_signal_level;
  float *rx_signal;

  struct {
    int index;
    double value;
  } leveltable[ANALOGTV_MAX_LINEHEIGHT+1][ANALOGTV_MAX_LINEHEIGHT+1];

  /* Only valid during draw. */
  unsigned random0, random1;
  double noiselevel;
  const analogtv_reception *const *recs;
  unsigned rec_count;

  float *signal_subtotals;

  float puheight;

  int test_five;

} analogtv;

#ifdef WIN32
PROTO_DLL analogtv *analogtv_allocate(HWND window);
#elif defined X11
analogtv *analogtv_allocate(Display *dpy, Window window);
#elif defined ALLEGRO
analogtv *analogtv_allocate();
#endif
PROTO_DLL analogtv_input *analogtv_input_allocate(void);
PROTO_DLL analogtv_reception *analogtv_reception_allocate(float level, analogtv_input *input);
PROTO_DLL void analogtv_reception_reallocate(analogtv_reception *rec, analogtv_input *input);

/* call if window size changes */
PROTO_DLL void analogtv_reconfigure(analogtv *it);

//PROTO_DLL void analogtv_set_defaults(analogtv *it);
PROTO_DLL void analogtv_release(analogtv *it);
PROTO_DLL int analogtv_set_demod(analogtv *it);
PROTO_DLL void analogtv_setup_frame(analogtv *it);
PROTO_DLL void analogtv_setup_sync(analogtv_input *input, int do_cb, int do_ssavi);
PROTO_DLL void analogtv_draw(analogtv *it, double noiselevel, const analogtv_reception *const *recs, unsigned rec_count);

#ifdef WIN32
PROTO_DLL int analogtv_load_ximage(analogtv *it, analogtv_input *input, HBITMAP pic_im);
#elif defined X11
int analogtv_load_ximage(analogtv *it, analogtv_input *input, XImage *pic_im);
#elif defined ALLEGRO
int analogtv_load_ximage(analogtv *it, analogtv_input *input, BITMAP *pic_im);
#endif

PROTO_DLL void analogtv_reception_update(analogtv_reception *inp);

PROTO_DLL void analogtv_setup_teletext(analogtv_input *input);


/* Functions for rendering content into an analogtv_input */

#ifdef WIN32
PROTO_DLL void analogtv_make_font(HWND window,
#elif defined X11
void analogtv_make_font(Display *dpy, Window window,
#elif defined ALLEGRO
void analogtv_make_font(
#endif
                        analogtv_font *f, int w, int h, char *fontname);
PROTO_DLL int analogtv_font_pixel(analogtv_font *f, int c, int x, int y);
#ifdef WIN32
PROTO_DLL void analogtv_font_set_pixel(analogtv_font *f, int c, int x, int y, int value, analogtv* it);
PROTO_DLL void analogtv_font_set_char(analogtv_font *f, int c, char *s, analogtv* it);
#else
PROTO_DLL void analogtv_font_set_pixel(analogtv_font *f, int c, int x, int y, int value);
PROTO_DLL void analogtv_font_set_char(analogtv_font *f, int c, char *s);
#endif
PROTO_DLL void analogtv_lcp_to_ntsc(double luma, double chroma, double phase,
                          int ntsc[4]);


PROTO_DLL void analogtv_draw_solid(analogtv_input *input,
                         int left, int right, int top, int bot,
                         int ntsc[4]);

PROTO_DLL void analogtv_draw_solid_rel_lcp(analogtv_input *input,
                                 double left, double right,
                                 double top, double bot,
                                 double luma, double chroma, double phase);

PROTO_DLL void analogtv_draw_char(analogtv_input *input, analogtv_font *f,
                        int c, int x, int y, int ntsc[4]);
PROTO_DLL void analogtv_draw_string(analogtv_input *input, analogtv_font *f,
                          char *s, int x, int y, int ntsc[4]);
PROTO_DLL void analogtv_draw_string_centered(analogtv_input *input, analogtv_font *f,
                                   char *s, int x, int y, int ntsc[4]);
PROTO_DLL void analogtv_draw_xpm(analogtv *tv, analogtv_input *input,
                       const char * const *xpm, int left, int top);

PROTO_DLL int analogtv_handle_events (analogtv *it);

PROTO_DLL void
analogtv_color(int idx, int ntsc[4]);

#ifdef HAVE_XSHM_EXTENSION
#define ANALOGTV_DEFAULTS_SHM "*useSHM:           True",
#else
#define ANALOGTV_DEFAULTS_SHM
#endif

#ifndef USE_IPHONE
# define ANALOGTV_DEF_BRIGHTNESS "2"
# define ANALOGTV_DEF_CONTRAST "150"
#else
  /* Need to really crank this up for it to look good on the iPhone screen. */
# define ANALOGTV_DEF_BRIGHTNESS "3"
# define ANALOGTV_DEF_CONTRAST "1000"
#endif

#define ANALOGTV_DEFAULTS \
  "*TVColor:         70", \
  "*TVTint:          5",  \
  "*TVBrightness:  " ANALOGTV_DEF_BRIGHTNESS,  \
  "*TVContrast:    " ANALOGTV_DEF_CONTRAST, \
  "*Background:      Black", \
  "*use_cmap:        0",  \
  "*geometry:	     800x600", \
  "*fpsSolid:	     True", \
  THREAD_DEFAULTS \
  ANALOGTV_DEFAULTS_SHM

#define ANALOGTV_OPTIONS \
  THREAD_OPTIONS \
  { "-use-cmap",        ".use_cmap",     XrmoptionSepArg, 0 }, \
  { "-tv-color",        ".TVColor",      XrmoptionSepArg, 0 }, \
  { "-tv-tint",         ".TVTint",       XrmoptionSepArg, 0 }, \
  { "-tv-brightness",   ".TVBrightness", XrmoptionSepArg, 0 }, \
  { "-tv-contrast",     ".TVContrast",   XrmoptionSepArg, 0 },

#endif /* _XSCREENSAVER_ANALOGTV_H */
