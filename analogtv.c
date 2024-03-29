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

/*

  This is the code for implementing something that looks like a conventional
  analog TV set. It simulates the following characteristics of standard
  televisions:

  - Realistic rendering of a composite video signal
  - Compression & brightening on the right, as the scan gets truncated
    because of saturation in the flyback transformer
  - Blooming of the picture dependent on brightness
  - Overscan, cutting off a few pixels on the left side.
  - Colored text in mixed graphics/text modes

  It's amazing how much it makes your high-end monitor look like at large
  late-70s TV. All you need is to put a big "Solid State" logo in curly script
  on it and you'd be set.

  In DirectColor or TrueColor modes, it generates pixel values
  directly from RGB values it calculates across each scan line. In
  PseudoColor mode, it consider each possible pattern of 5 preceding
  bit values in each possible position modulo 4 and allocates a color
  for each. A few things, like the brightening on the right side as
  the horizontal trace slows down, aren't done in PseudoColor.

  I originally wrote it for the Apple ][ emulator, and generalized it
  here for use with a rewrite of xteevee and possibly others.

  A maxim of technology is that failures reveal underlying mechanism.
  A good way to learn how something works is to push it to failure.
  The way it fails will usually tell you a lot about how it works. The
  corollary for this piece of software is that in order to emulate
  realistic failures of a TV set, it has to work just like a TV set.
  So there is lots of DSP-style emulation of analog circuitry for
  things like color decoding, H and V sync following, and more. In
  2003, computers are just fast enough to do this at television signal
  rates. We use a 14 MHz sample rate here, so we can do on the order
  of a couple hundred instructions per sample and keep a good frame
  rate.

  Trevor Blackwell <tlb@tlb.org>
*/

/*
  2014-04-20, Dave Odell <dmo2118@gmail.com>:
  API change: Folded analogtv_init_signal and *_add_signal into *_draw().
  Added SMP support.
  Replaced doubles with floats, including constants and transcendental functions.
  Fixed a bug or two.
*/

/* 2015-02-27, Tomasz Sulej <tomeksul@gmail.com>:
   - tint_control variable is used now
   - removed unusable hashnoise code
 */

char* progname = "analogtv";

#ifdef WIN32
# include <windows.h>
#elif defined HAVE_COCOA
# include "jwxyz.h"
#elif defined X11
# include <X11/Xlib.h>
# include <X11/Xutil.h>
#endif
#include <limits.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
//#include "utils.h"
//#include "resources.h"
#include "analogtv.h"
#include "yarandom.h"
//#include "grabscreen.h"
#ifdef X11
#include "visual.h"
#endif
/* #define DEBUG 1 */

#if 0 && defined(DEBUG) && (defined(__linux) || defined(__FreeBSD__))
/* only works on linux + freebsd */
#include <machine/cpufunc.h>

#define DTIME_DECL u_int64_t dtimes[100]; int n_dtimes
#define DTIME_START do {n_dtimes=0; dtimes[n_dtimes++]=rdtsc(); } while (0)
#define DTIME dtimes[n_dtimes++]=rdtsc()
#define DTIME_SHOW(DIV) \
do { \
  double _dtime_div=(DIV); \
  printf("time/%.1f: ",_dtime_div); \
  for (i=1; i<n_dtimes; i++) \
    printf(" %0.9f",(dtimes[i]-dtimes[i-1])* 1e-9 / _dtime_div); \
  printf("\n"); \
} while (0)

#else

#define DTIME_DECL
#define DTIME_START  do { } while (0)
#define DTIME  do { } while (0)
#define DTIME_SHOW(DIV)  do { } while (0)

#endif

#ifdef X11
#define PIXEL_DRAW_WIDTH 2
#endif

#ifdef WIN32
#if 0
static void bitmap_blit_pixel(HDC hdc, int x, int y, int color) {
	SetPixelV()
}
#else
#define bitmap_blit_pixel(hdc, x, y, color) \
	SetPixelV(hdc, x, y, color);
#endif

#define BM_SIZE_WIDTH(size) \
	((size & 0xffff0000) >> 16)
#define BM_SIZE_HEIGHT(size) \
	(size & 0x0000ffff)

#define BM_PIXEL_WIDTH 3
//#define BM_PIXEL_DRAW_WIDTH 6

byte *bitmap_blast_data;
HDC bitmap_blast_hdcMem;
unsigned bitmap_blast_width;
unsigned bitmap_blast_height;
unsigned bitmap_blast_bytes;

BITMAPINFOHEADER bitmap_blast_header;
BITMAPINFO bitmap_blast_info;

/*
 * Returns size as two 16-bit numbers.
 * && 0xff00 = width
 * && 0x00ff = height
 */
static unsigned long long bitmap_get_window_size(HWND window) {
	RECT windrect;
	GetWindowRect(window, &windrect);
	unsigned long long width = windrect.right - windrect.left;
	width = width << 16;
	unsigned long long height = ((windrect.bottom - windrect.top) & 0xffff);
	return width | height;
}

#endif

/*
 * Intensity varies from 1.0 (dark) to 0.1 (light). Default is 0.8.
 */
#ifdef WIN32
#define FLOAT_INTENSITY 0.6
#else
#define FLOAT_INTENSITY 0.8
#endif

#define FASTRND_A 1103515245
#define FASTRND_C 12345
#define FASTRND (fastrnd = fastrnd*FASTRND_A+FASTRND_C)

static void analogtv_ntsc_to_yiq(const analogtv *it, int lineno, const float *signal,
                                 int start, int end, struct analogtv_yiq_s *it_yiq);

static float puramp(const analogtv *it, float tc, float start, float over)
{
  float pt=it->powerup-start;
  float ret;
  if (pt<0.0f) return 0.0f;
  if (pt>900.0f || pt/tc>8.0f) return 1.0f;

  ret=(1.0f-expf(-pt/tc))*over;
  if (ret>1.0f) return 1.0f;
  return ret*ret;
}

/*
  There are actual standards for TV signals: NTSC and RS-170A describe the
  system used in the US and Japan. Europe has slightly different systems, but
  not different enough to make substantially different screensaver displays.
  Sadly, the standards bodies don't do anything so useful as publish the spec on
  the web. Best bets are:

    http://www.ee.washington.edu/conselec/CE/kuhn/ntsc/95x4.htm
    http://www.ntsc-tv.com/ntsc-index-02.htm

  In DirectColor or TrueColor modes, it generates pixel values directly from RGB
  values it calculates across each scan line. In PseudoColor mode, it consider
  each possible pattern of 5 preceding bit values in each possible position
  modulo 4 and allocates a color for each. A few things, like the brightening on
  the right side as the horizontal trace slows down, aren't done in PseudoColor.

  I'd like to add a bit of visible retrace, but it conflicts with being able to
  bitcopy the image when fast scrolling. After another couple of CPU
  generations, we could probably regenerate the whole image from scratch every
  time. On a P4 2 GHz it can manage this fine for blinking text, but scrolling
  looks too slow.
*/

/* localbyteorder is MSBFirst or LSBFirst */
static int localbyteorder;
static const double float_low8_ofs=8388608.0;
static int float_extraction_works;

typedef union {
  float f;
  int i;
} float_extract_t;

static void
analogtv_init(void)
{
  ya_rand_init(725646277);

  int i;
  {
    unsigned int localbyteorder_loc = (MSBFirst<<24) | (LSBFirst<<0);
    localbyteorder=*(char *)&localbyteorder_loc;
  }

  if (1) {
    float_extract_t fe;
    int ans;

    float_extraction_works=1;
    for (i=0; i<256*4; i++) {
      fe.f=(float)(float_low8_ofs+(double)i);
      ans=fe.i&0x3ff;
      if (ans != i) {
#ifdef DEBUG
        printf("Float extraction failed for %d => %d\n",i,ans);
#endif
        float_extraction_works=0;
        break;
      }
    }
  }

}

static void
analogtv_set_defaults(analogtv *it)
{
  //char buf[256];

  //sprintf(buf,"%sTVTint",prefix);
  /* it->tint_control = get_float_resource(it->dpy, buf,"TVTint"); */
  it->tint_control = 5.0;
  //sprintf(buf,"%sTVColor",prefix);
  /* it->color_control = get_float_resource(it->dpy, buf,"TVColor")/100.0; */
  it->color_control = (float)(70.0/100.0);
  //sprintf(buf,"%sTVBrightness",prefix);
  /* it->brightness_control = get_float_resource(it->dpy, buf,"TVBrightness") / 100.0; */
  it->brightness_control = (float)(2.0 / 100.0);
  //sprintf(buf,"%sTVContrast",prefix);
  /* it->contrast_control = get_float_resource(it->dpy, buf,"TVContrast") / 100.0; */
  it->contrast_control = 150.0 / 100.0;
  it->height_control = 1.0;
  it->width_control = 1.0;
  it->squish_control = 0.0;
  it->powerup=1000.0;

  it->hashnoise_rpm=0;
  it->hashnoise_on=0;
  it->hashnoise_enable=1;

  it->horiz_desync=(float)(frand(10.0)-5.0);
  it->squeezebottom=(float)(frand(5.0)-1.0);

#ifdef DEBUG
#ifdef VERBOSE
  printf("analogtv: prefix=%s\n",prefix);
  printf("  use: shm=%d cmap=%d color=%d\n",
         it->use_shm,it->use_cmap,it->use_color);
  printf("  controls: tint=%g color=%g brightness=%g contrast=%g\n",
         it->tint_control, it->color_control, it->brightness_control,
         it->contrast_control);
/*  printf("  freq_error %g: %g %d\n",
         it->freq_error, it->freq_error_inc, it->flutter_tint); */
  printf("  desync: %g %d\n",
         it->horiz_desync, it->flutter_horiz_desync);
  printf("  hashnoise rpm: %g\n",
         it->hashnoise_rpm);
  /* printf("  vis: %d %d %d\n",
         it->visclass, it->visbits, it->visdepth); */
  printf("  shift: %d-%d %d-%d %d-%d\n",
         it->red_invprec,it->red_shift,
         it->green_invprec,it->green_shift,
         it->blue_invprec,it->blue_shift);
  printf("  size: %d %d  %d %d  xrepl=%d\n",
         it->usewidth, it->useheight,
         it->screen_xo, it->screen_yo, it->xrepl);

  printf("    ANALOGTV_V=%d\n",ANALOGTV_V);
  printf("    ANALOGTV_TOP=%d\n",ANALOGTV_TOP);
  printf("    ANALOGTV_VISLINES=%d\n",ANALOGTV_VISLINES);
  printf("    ANALOGTV_BOT=%d\n",ANALOGTV_BOT);
  printf("    ANALOGTV_H=%d\n",ANALOGTV_H);
  printf("    ANALOGTV_SYNC_START=%d\n",ANALOGTV_SYNC_START);
  printf("    ANALOGTV_BP_START=%d\n",ANALOGTV_BP_START);
  printf("    ANALOGTV_CB_START=%d\n",ANALOGTV_CB_START);
  printf("    ANALOGTV_PIC_START=%d\n",ANALOGTV_PIC_START);
  printf("    ANALOGTV_PIC_LEN=%d\n",ANALOGTV_PIC_LEN);
  printf("    ANALOGTV_FP_START=%d\n",ANALOGTV_FP_START);
  printf("    ANALOGTV_PIC_END=%d\n",ANALOGTV_PIC_END);
  printf("    ANALOGTV_HASHNOISE_LEN=%d\n",ANALOGTV_HASHNOISE_LEN);
#endif
#endif

}

static void
analogtv_free_image(analogtv *it)
{
  if (it->image) {
#ifdef WIN32
    DeleteObject(it->image);
#elif defined X11
    if (it->use_shm) {
#ifdef HAVE_XSHM_EXTENSION
      destroy_xshm_image(it->dpy, it->image, &it->shm_info);
#endif
    } else {
      thread_free(it->image->data);
      it->image->data = NULL;
      XDestroyImage(it->image);
    }
#elif defined ALLEGRO
    destroy_bitmap(it->image);
#endif
    it->image=NULL;
  }
}

static void
analogtv_alloc_image(analogtv *it)
{
  /* On failure, it->image is NULL. */

#ifdef WIN32
  HDC dpy = GetDC(it->window);
  unsigned bits_per_pixel = GetDeviceCaps(dpy, BITSPIXEL) * GetDeviceCaps(dpy, PLANES);
  unsigned align = thread_memory_alignment(dpy) * 8 - 1;
  /*
  unsigned long long size = bitmap_get_window_size(it->window);
  unsigned long screen_width = (unsigned long)BM_SIZE_WIDTH(size);
  unsigned long screen_height = (unsigned long)BM_SIZE_HEIGHT(size);
  */
#elif defined X11
  unsigned bits_per_pixel = get_bits_per_pixel(it->dpy, it->xgwa.depth);
  unsigned align = thread_memory_alignment(it->dpy) * 8 - 1;
  /*
  unsigned screen_width = it->xgwa.width;
  unsigned screen_height = it->xgwa.height;
  */
#elif defined ALLEGRO
  unsigned bits_per_pixel = bitmap_color_depth(screen);
  unsigned align = thread_memory_alignment(screen) * 8 - 1;
#endif

  if (it->use_shm) {
#ifdef HAVE_XSHM_EXTENSION
    it->image=create_xshm_image(it->dpy, it->xgwa.visual, it->xgwa.depth, ZPixmap, 0,
                                &it->shm_info,
                                width / bits_per_pixel, it->useheight);
#endif
    if (!it->image) it->use_shm=0;
  }
  if (!it->image) {
#ifdef WIN32
	it->image = CreateBitmap(it->usewidth, it->useheight, GetDeviceCaps(dpy, PLANES), GetDeviceCaps(dpy, BITSPIXEL), NULL);

	bitmap_blast_width = it->usewidth * it->xrepl * BM_PIXEL_WIDTH;
    bitmap_blast_height = 3;
    bitmap_blast_bytes = bitmap_blast_width * bitmap_blast_height * 3 * 3;

	//ZeroMemory(&bmpi, sizeof(BITMAPINFO));
	//ZeroMemory(&bmih, sizeof(BITMAPINFOHEADER));
	bitmap_blast_header.biBitCount = 24;
	bitmap_blast_header.biCompression = BI_RGB;
	bitmap_blast_header.biWidth = it->usewidth;
	bitmap_blast_header.biHeight = 3;
	bitmap_blast_header.biPlanes = GetDeviceCaps(dpy, PLANES);
	bitmap_blast_header.biSize = 40;
	bitmap_blast_header.biSizeImage = bitmap_blast_bytes; // BM_SIZE_WIDTH(size) * 3 * 3;
	bitmap_blast_header.biClrImportant = 0;
	bitmap_blast_header.biClrUsed = 0;
	bitmap_blast_info.bmiHeader = bitmap_blast_header;

#elif defined X11
    /* Width is in bits. */
    unsigned width = (it->usewidth * bits_per_pixel + align) & ~align;

    it->image = XCreateImage(it->dpy, it->xgwa.visual, it->xgwa.depth, ZPixmap, 0, 0,
                             it->usewidth, it->useheight, 8, width / 8);
    if (it->image) {
      if(thread_malloc((void **)&it->image->data, it->dpy,
                       it->image->height * it->image->bytes_per_line)) {
        it->image->data = NULL;
        XDestroyImage(it->image);
        it->image = NULL;
      }
    }
#elif defined ALLEGRO
    it->image = create_video_bitmap(it->usewidth, it->useheight);
#endif
  }

#ifdef X11
  if (it->image) {
    memset (it->image->data, 0, it->image->height * it->image->bytes_per_line);
  } else {
    /* Not enough memory. Maybe try a smaller window. */
    fprintf(stderr, "analogtv: %s\n", strerror(ENOMEM));
  }
#endif

#ifdef WIN32
  ReleaseDC(it->window, dpy);
#endif
}

PROTO_DLL void
analogtv_free_imagefile(unsigned *data) {
  if (NULL != data) {
    free(data);
  }
}

static void
analogtv_configure(analogtv *it)
{
  int oldwidth=it->usewidth;
  int oldheight=it->useheight;
  int height_diff;

  /* If the window is very small, don't let the image we draw get lower
     than the actual TV resolution (266x200.)

     If the aspect ratio of the window is close to a 4:3 or 16:9 ratio,
     then scale the image to exactly fill the window.

     Otherwise, center the image either horizontally or vertically,
     letterboxing or pillarboxing (but not both).

     If it's very close (2.5%) to a multiple of VISLINES, make it exact
     For example, it maps 1024 => 1000.
   */
  float percent = 0.15f;
  float min_ratio = (float)(4.0 / 3.0 * (1 - percent));
  float max_ratio = (float)(4.0 / 3.0 * (1 + percent));
  //float max_ratio = 16.0 / 9.0 * (1 + percent);
  float ratio;
  float height_snap=0.025f;

#ifdef WIN32
  unsigned long long size = bitmap_get_window_size(it->window);
  int wlim = (int)BM_SIZE_WIDTH(size);
  int hlim = (int)BM_SIZE_HEIGHT(size);
#elif defined X11
  int hlim = it->xgwa.height;
  int wlim = it->xgwa.width;
#elif defined ALLEGRO
  int hlim = SCREEN_H;
  int wlim = SCREEN_W;
#endif
  int surface_width = wlim;
  int surface_height = hlim;

  ratio = wlim / (float)hlim;

#ifdef USE_IPHONE
  /* Fill the whole iPhone screen, even though that distorts the image. */
  min_ratio = 0;
  max_ratio = 10;
#endif

  if (wlim < 266 || hlim < 200)
    {
      wlim = 266;
      hlim = 200;
# ifdef DEBUG
      fprintf (stderr,
               "size: minimal: %dx%d in %dx%d (%.3f < %.3f < %.3f)\n",
               wlim, hlim, surface_width, surface_height,
               min_ratio, ratio, max_ratio);
# endif
    }
  else if (ratio > min_ratio && ratio < max_ratio)
    {
# ifdef DEBUG
      fprintf (stderr,
               "size: close enough: %dx%d (%.3f < %.3f < %.3f)\n",
               wlim, hlim, min_ratio, ratio, max_ratio);
# endif
    }
  else if (ratio >= max_ratio)
    {
      wlim = (int)(hlim*max_ratio);
# ifdef DEBUG
      fprintf (stderr,
               "size: center H: %dx%d in %dx%d (%.3f < %.3f < %.3f)\n",
               wlim, hlim, surface_width, surface_height,
               min_ratio, ratio, max_ratio);
# endif
    }
  else /* ratio <= min_ratio */
    {
      hlim = (int)(wlim/min_ratio);
# ifdef DEBUG
      fprintf (stderr,
               "size: center V: %dx%d in %dx%d (%.3f < %.3f < %.3f)\n",
               wlim, hlim, surface_width, surface_height,
               min_ratio, ratio, max_ratio);
# endif
    }


  height_diff = ((hlim + ANALOGTV_VISLINES/2) % ANALOGTV_VISLINES) - ANALOGTV_VISLINES/2;
  if (height_diff != 0 && fabs(height_diff) < hlim * height_snap)
    {
      hlim -= height_diff;
    }

  /* SetDIBitsToDevice requires rows to be aligned on a DWORD (4 bytes). */
  wlim /= 4;
  wlim *= 4;

#if DEBUG
  /* The scanlines get wonky and irregular if they are not multiples of 395 (790?) */
  //assert(0 == (surface_height % 395));

  /* We want to be roughly 4:3 ratio or the image will get wonky. */
  /*
  int width_ratio_ideal = surface_height * 4 / 3;
  int width_limit_high = width_ratio_ideal + 20;
  int width_limit_low = width_ratio_ideal - 20;
  */
  //assert(surface_width > width_limit_low);
  //assert(surface_width < width_limit_high);
#endif

  hlim /= ANALOGTV_VISLINES;
  hlim *= ANALOGTV_VISLINES;

  //hlim /= 4;
  //hlim *= 4;

  /* TODO: Move our constant checks from image_alloc() to here? */

  /* Most times this doesn't change */
  if (wlim != oldwidth || hlim != oldheight) {

    it->usewidth=wlim;
    it->useheight=hlim;

    it->xrepl=1+it->usewidth/640;
    if (it->xrepl>2) it->xrepl=2;
    it->subwidth=it->usewidth/it->xrepl;

    analogtv_free_image(it);
    analogtv_alloc_image(it);
  }

#ifdef X11
  it->screen_xo = (it->xgwa.width-it->usewidth)/2;
  it->screen_yo = (it->xgwa.height-it->useheight)/2;
#elif defined WIN32 || defined ALLEGRO
  it->screen_xo = (surface_width-it->usewidth)/2;
  it->screen_yo = (surface_height-it->useheight)/2;
#endif
  it->need_clear=1;
}

void
analogtv_reconfigure(analogtv *it)
{
#ifdef X11
	XGetWindowAttributes(it->dpy, it->window, &it->xgwa);
    XClearArea(it->dpy, it->window, 0, 0, it->xgwa.width, it->xgwa.height, 0);
#endif
	analogtv_configure(it);
}

/* Can be any power-of-two <= 32. 16 a slightly better choice for 2-3 threads. */
#define ANALOGTV_SUBTOTAL_LEN 32

typedef struct analogtv_thread_s
{
  analogtv *it;
  unsigned thread_id;
  size_t signal_start, signal_end;
} analogtv_thread;

#define SIGNAL_OFFSET(thread_id) \
  ((ANALOGTV_SIGNAL_LEN * (thread_id) / threads->count) & align)

static int analogtv_thread_create(void *thread_raw, struct threadpool *threads,
                                  unsigned thread_id)
{
  analogtv_thread *thread = (analogtv_thread *)thread_raw;
  unsigned align;

  thread->it = GET_PARENT_OBJ(analogtv, threads, threads);
  thread->thread_id = thread_id;

#ifdef WIN32
  align = thread_memory_alignment(NULL) /
            sizeof(thread->it->signal_subtotals[0]);
#elif defined X11
  align = thread_memory_alignment(thread->it->dpy) /
	  sizeof(thread->it->signal_subtotals[0]);
#elif defined ALLEGRO
  align = thread_memory_alignment(screen) /
	  sizeof(thread->it->signal_subtotals[0]);
#endif
  if (!align)
    align = 1;
  align = ~(align * ANALOGTV_SUBTOTAL_LEN - 1);

  thread->signal_start = SIGNAL_OFFSET(thread_id);
  thread->signal_end = thread_id + 1 == threads->count ?
                       ANALOGTV_SIGNAL_LEN :
                       SIGNAL_OFFSET(thread_id + 1);

  return 0;
}

static void analogtv_thread_destroy(void *thread_raw)
{
}

#ifdef WIN32
PROTO_DLL analogtv *analogtv_allocate(HWND window)
#elif defined X11
analogtv *analogtv_allocate(Display *dpy, Window window)
#elif defined ALLEGRO
analogtv *analogtv_allocate()
#endif
{
  static const struct threadpool_class cls = {
    sizeof(analogtv_thread),
    analogtv_thread_create,
    analogtv_thread_destroy
  };
#ifdef X11
  XGCValues gcv;
#endif
  analogtv *it = NULL;
  int i;
  const size_t rx_signal_len = ANALOGTV_SIGNAL_LEN + 2*ANALOGTV_H;

  analogtv_init();

  it=(analogtv *)calloc(1,sizeof(analogtv));
  if (!it) return 0;
  it->threads.count=0;
  it->rx_signal=NULL;
  it->signal_subtotals=NULL;
  it->test_five = 5;

#ifdef X11
  it->dpy=dpy;
#elif defined ALLEGRO
  it->dpy = screen;
#endif
#ifndef ALLEGRO
  it->window=window;
#endif

  if (thread_malloc((void **)&it->rx_signal, it->dpy,
                    sizeof(it->rx_signal[0]) * rx_signal_len))
    goto fail;

  assert(!(ANALOGTV_SIGNAL_LEN % ANALOGTV_SUBTOTAL_LEN));
  if (thread_malloc((void **)&it->signal_subtotals, it->dpy,
                    sizeof(it->signal_subtotals[0]) *
                     (rx_signal_len / ANALOGTV_SUBTOTAL_LEN)))
    goto fail;

#ifdef WIN32
  if (threadpool_create(&it->threads, &cls, hardware_concurrency()))
#elif defined X11 || defined ALLEGRO
  if (threadpool_create(&it->threads, &cls, it->dpy, hardware_concurrency(it->dpy)))
#endif
	  goto fail;

  assert(it->threads.count);

  it->shrinkpulse=-1;

  it->n_colors=0;

#ifdef HAVE_XSHM_EXTENSION
  it->use_shm=1;
#else
  it->use_shm=0;
#endif

#ifdef WIN32
  it->use_cmap = 0;
  it->use_color = 1;

  it->red_mask = 0x000000ff;
  it->green_mask = 0x0000ff00;
  it->blue_mask = 0x00ff0000;
  it->red_shift = it->red_invprec = -1;
  it->green_shift = it->green_invprec = -1;
  it->blue_shift = it->blue_invprec = -1;
#elif defined X11
  XGetWindowAttributes(it->dpy, it->window, &it->xgwa);

  it->screen = it->xgwa.screen;
  it->colormap = it->xgwa.colormap;
  it->visclass = it->xgwa.visual->class;
  it->visbits = it->xgwa.visual->bits_per_rgb;
  it->visdepth = it->xgwa.depth;
  if (it->visclass == TrueColor || it->visclass == DirectColor) {
#if 0
    if (get_integer_resource (it->dpy, "use_cmap", "Integer")) {
      it->use_cmap=1;
    } else {
#endif
      it->use_cmap=0;
#if 0
    }
    it->use_color=!mono_p;
#endif
    it->use_color=1;
  }
  else if (it->visclass == PseudoColor || it->visclass == StaticColor) {
    it->use_cmap=1;
    //it->use_color=!mono_p;
    it->use_color=1;
  }
  else {
    it->use_cmap=1;
    it->use_color=0;
  }

  it->red_mask=it->xgwa.visual->red_mask;
  it->green_mask=it->xgwa.visual->green_mask;
  it->blue_mask=it->xgwa.visual->blue_mask;
  it->red_shift=it->red_invprec=-1;
  it->green_shift=it->green_invprec=-1;
  it->blue_shift=it->blue_invprec=-1;
#elif defined ALLEGRO
  it->use_cmap = 0;
  it->use_color = 1;

  it->red_mask = 0x000000ff;
  it->green_mask = 0x0000ff00;
  it->blue_mask = 0x00ff0000;
  it->red_shift = it->red_invprec = -1;
  it->green_shift = it->green_invprec = -1;
  it->blue_shift = it->blue_invprec = -1;
#endif

  analogtv_configure(it);

  if (!it->use_cmap) {
    /* Is there a standard way to do this? Does this handle all cases? */
    int shift, prec;
    for (shift=0; shift<32; shift++) {
      for (prec=1; prec<16 && prec<40-shift; prec++) {
        unsigned long mask=(0xffffUL>>(16-prec)) << shift;
        if (it->red_shift<0 && mask==it->red_mask)
          it->red_shift=shift, it->red_invprec=16-prec;
        if (it->green_shift<0 && mask==it->green_mask)
          it->green_shift=shift, it->green_invprec=16-prec;
        if (it->blue_shift<0 && mask==it->blue_mask)
          it->blue_shift=shift, it->blue_invprec=16-prec;
      }
    }
    if (it->red_shift<0 || it->green_shift<0 || it->blue_shift<0) {
      if (0) fprintf(stderr,"Can't figure out color space\n");
      goto fail;
    }

    for (i=0; i<ANALOGTV_CV_MAX; i++) {
      int intensity=(int)(pow(i/256.0, FLOAT_INTENSITY)*65535.0); /* gamma correction */
      if (intensity>65535) intensity=65535;
      it->red_values[i]=((intensity>>it->red_invprec)<<it->red_shift);
      it->green_values[i]=((intensity>>it->green_invprec)<<it->green_shift);
      it->blue_values[i]=((intensity>>it->blue_invprec)<<it->blue_shift);
    }
  }

/*
  gcv.background=get_pixel_resource(it->dpy, it->colormap,
                                    "background", "Background");
*/

#ifdef X11
  XColor color;
  color.flags = DoRed|DoGreen|DoBlue;
  color.red = color.green = color.blue = 0; /* Black */
  XAllocColor(it->dpy, it->colormap, &color);
  gcv.background = (unsigned int)color.pixel;

  it->gc = XCreateGC(it->dpy, it->window, GCBackground, &gcv);
  XSetWindowBackground(it->dpy, it->window, gcv.background);
  XClearWindow(dpy,window);
#endif

  analogtv_configure(it);

  analogtv_set_defaults(it);

  return it;

 fail:
  if (it) {
    if(it->threads.count)
      threadpool_destroy(&it->threads);
    thread_free(it->signal_subtotals);
    thread_free(it->rx_signal);
    free(it);
  }
  return NULL;
}

void
analogtv_release(analogtv *it)
{
  if (it->image) {
#ifdef WIN32
    DeleteObject(it->image);
#elif defined X11
    if (it->use_shm) {
#ifdef HAVE_XSHM_EXTENSION
      destroy_xshm_image(it->dpy, it->image, &it->shm_info);
#endif
    } else {
      thread_free(it->image->data);
      it->image->data = NULL;
      XDestroyImage(it->image);
    }
#elif defined ALLEGRO
    destroy_bitmap(it->image);
#endif
    it->image=NULL;
  }
#ifdef X11
  if (it->gc) XFreeGC(it->dpy, it->gc);
  it->gc=NULL;
  if (it->n_colors) XFreeColors(it->dpy, it->colormap, it->colors, it->n_colors, 0L);
#endif
  it->n_colors=0;
  threadpool_destroy(&it->threads);
  thread_free(it->rx_signal);
  thread_free(it->signal_subtotals);
  free(it);
}


/*
  First generate the I and Q reference signals, which we'll multiply
  the input signal by to accomplish the demodulation. Normally they
  are shifted 33 degrees from the colorburst. I think this was convenient
  for inductor-capacitor-vacuum tube implementation.

  The tint control, FWIW, just adds a phase shift to the chroma signal,
  and the color control controls the amplitude.

  In text modes (colormode==0) the system disabled the color burst, and no
  color was detected by the monitor.

  freq_error gives a mismatch between the built-in oscillator and the
  TV's colorbust. Some II Plus machines seemed to occasionally get
  instability problems -- the crystal oscillator was a single
  transistor if I remember correctly -- and the frequency would vary
  enough that the tint would change across the width of the screen.
  The left side would be in correct tint because it had just gotten
  resynchronized with the color burst.

  If we're using a colormap, set it up.
*/
PROTO_DLL int
analogtv_set_demod(analogtv *it)
{
#ifdef X11
  int y_levels=10,i_levels=5,q_levels=5;

  /*
    In principle, we might be able to figure out how to adjust the
    color map frame-by-frame to get some nice color bummage. But I'm
    terrified of changing the color map because we'll get flashing.

    I can hardly believe we still have to deal with colormaps. They're
    like having NEAR PTRs: an enormous hassle for the programmer just
    to save on memory. They should have been deprecated by 1995 or
    so. */

 cmap_again:
  if (it->use_cmap && !it->n_colors) {

    if (it->n_colors) {
      XFreeColors(it->dpy, it->colormap, it->colors, it->n_colors, 0L);
      it->n_colors=0;
    }

    {
      int yli,qli,ili;
      for (yli=0; yli<y_levels; yli++) {
        for (ili=0; ili<i_levels; ili++) {
          for (qli=0; qli<q_levels; qli++) {
            double interpy,interpi,interpq;
            double levelmult=700.0;
            int r,g,b;
            XColor col;

            interpy=100.0 * ((double)yli/y_levels);
            interpi=50.0 * (((double)ili-(0.5*i_levels))/(double)i_levels);
            interpq=50.0 * (((double)qli-(0.5*q_levels))/(double)q_levels);

            r=(int)((interpy + 1.04*interpi + 0.624*interpq)*levelmult);
            g=(int)((interpy - 0.276*interpi - 0.639*interpq)*levelmult);
            b=(int)((interpy - 1.105*interpi + 1.729*interpq)*levelmult);
            if (r<0) r=0;
            if (r>65535) r=65535;
            if (g<0) g=0;
            if (g>65535) g=65535;
            if (b<0) b=0;
            if (b>65535) b=65535;

#ifdef DEBUG
            printf("%0.2f %0.2f %0.2f => %02x%02x%02x\n",
                   interpy, interpi, interpq,
                   r/256,g/256,b/256);
#endif

            col.red=r;
            col.green=g;
            col.blue=b;
            col.pixel=0;
            if (!XAllocColor(it->dpy, it->colormap, &col)) {
              if (q_levels > y_levels*4/12)
                q_levels--;
              else if (i_levels > y_levels*5/12)
                i_levels--;
              else
                y_levels--;

              if (y_levels<2)
                return -1;
              goto cmap_again;
            }
            it->colors[it->n_colors++]=col.pixel;
          }
        }
      }

      it->cmap_y_levels=y_levels;
      it->cmap_i_levels=i_levels;
      it->cmap_q_levels=q_levels;
    }
  }
#endif

  return 0;
}

#if 0
unsigned int
analogtv_line_signature(analogtv_input *input, int lineno)
{
  int i;
  char *origsignal=&input->signal[(lineno+input->vsync)
                                  %ANALOGTV_V][input->line_hsync[lineno]];
  unsigned int hash=0;

  /* probably lame */
  for (i=0; i<ANALOGTV_PIC_LEN; i++) {
    int c=origsignal[i];
    hash = hash + (hash<<17) + c;
  }

  hash += input->line_hsync[lineno];
  hash ^= hash >> 2;
  /*
  hash += input->hashnoise_times[lineno];
  hash ^= hash >> 2;
  */

  return hash;
}
#endif


/* Here we model the analog circuitry of an NTSC television.
   Basically, it splits the signal into 3 signals: Y, I and Q. Y
   corresponds to luminance, and you get it by low-pass filtering the
   input signal to below 3.57 MHz.

   I and Q are the in-phase and quadrature components of the 3.57 MHz
   subcarrier. We get them by multiplying by cos(3.57 MHz*t) and
   sin(3.57 MHz*t), and low-pass filtering. Because the eye has less
   resolution in some colors than others, the I component gets
   low-pass filtered at 1.5 MHz and the Q at 0.5 MHz. The I component
   is approximately orange-blue, and Q is roughly purple-green. See
   http://www.ntsc-tv.com for details.

   We actually do an awful lot to the signal here. I suspect it would
   make sense to wrap them all up together by calculating impulse
   response and doing FFT convolutions.

*/

static void
analogtv_ntsc_to_yiq(const analogtv *it, int lineno, const float *signal,
                     int start, int end, struct analogtv_yiq_s *it_yiq)
{
  enum {MAXDELAY=32};
  int i;
  const float *sp;
  int phasecorr=(signal-it->rx_signal)&3;
  struct analogtv_yiq_s *yiq;
  int colormode;
  float agclevel=it->agclevel;
  float brightadd= (float)(it->brightness_control*100.0 - ANALOGTV_BLACK_LEVEL);
  float delay[MAXDELAY+ANALOGTV_PIC_LEN], *dp;
  float multiq2[4];

  {

    double cb_i=(it->line_cb_phase[lineno][(2+phasecorr)&3]-
                 it->line_cb_phase[lineno][(0+phasecorr)&3])/16.0;
    double cb_q=(it->line_cb_phase[lineno][(3+phasecorr)&3]-
                 it->line_cb_phase[lineno][(1+phasecorr)&3])/16.0;

    colormode = (cb_i * cb_i + cb_q * cb_q) > 2.8;

    if (colormode) {
      multiq2[0] = (float)((cb_i*it->tint_i - cb_q*it->tint_q) * it->color_control);
      multiq2[1] = (float)((cb_q*it->tint_i + cb_i*it->tint_q) * it->color_control);
      multiq2[2]=-multiq2[0];
      multiq2[3]=-multiq2[1];
    }
  }

#if 0
  if (lineno==100) {
    printf("multiq = [%0.3f %0.3f %0.3f %0.3f] ",
           it->multiq[60],it->multiq[61],it->multiq[62],it->multiq[63]);
    printf("it->line_cb_phase = [%0.3f %0.3f %0.3f %0.3f]\n",
           it->line_cb_phase[lineno][0],it->line_cb_phase[lineno][1],
           it->line_cb_phase[lineno][2],it->line_cb_phase[lineno][3]);
    printf("multiq2 = [%0.3f %0.3f %0.3f %0.3f]\n",
           multiq2[0],multiq2[1],multiq2[2],multiq2[3]);
  }
#endif

  dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
  for (i=0; i<5; i++) dp[i]=0.0f;

  assert(start>=0);
  assert(end < ANALOGTV_PIC_LEN+10);

  dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
  for (i=0; i<24; i++) dp[i]=0.0;
  for (i=start, yiq=it_yiq+start, sp=signal+start;
       i<end;
       i++, dp--, yiq++, sp++) {

    /* Now filter them. These are infinite impulse response filters
       calculated by the script at
       http://www-users.cs.york.ac.uk/~fisher/mkfilter. This is
       fixed-point integer DSP, son. No place for wimps. We do it in
       integer because you can count on integer being faster on most
       CPUs. We care about speed because we need to recalculate every
       time we blink text, and when we spew random bytes into screen
       memory. This is roughly 16.16 fixed point arithmetic, but we
       scale some filter values up by a few bits to avoid some nasty
       precision errors. */

    /* Filter Y with a 4-pole low-pass Butterworth filter at 3.5 MHz
       with an extra zero at 3.5 MHz, from
       mkfilter -Bu -Lp -o 4 -a 2.1428571429e-01 0 -Z 2.5e-01 -l
       Delay about 2 */

    dp[0] = sp[0] * 0.0469904257251935f * agclevel;
    dp[8] = (+1.0f*(dp[6]+dp[0])
             +4.0f*(dp[5]+dp[1])
             +7.0f*(dp[4]+dp[2])
             +8.0f*(dp[3])
             -0.0176648f*dp[12]
             -0.4860288f*dp[10]);
    yiq->y = dp[8] + brightadd;
  }

  if (colormode) {
    dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
    for (i=0; i<27; i++) dp[i]=0.0;

    for (i=start, yiq=it_yiq+start, sp=signal+start;
         i<end;
         i++, dp--, yiq++, sp++) {
      float sig=*sp;

      /* Filter I and Q with a 3-pole low-pass Butterworth filter at
         1.5 MHz with an extra zero at 3.5 MHz, from
         mkfilter -Bu -Lp -o 3 -a 1.0714285714e-01 0 -Z 2.5000000000e-01 -l
         Delay about 3.
      */

      dp[0] = sig*multiq2[i&3] * 0.0833333333333f;
      yiq->i=dp[8] = (dp[5] + dp[0]
                      +3.0f*(dp[4] + dp[1])
                      +4.0f*(dp[3] + dp[2])
                      -0.3333333333f * dp[10]);

      dp[16] = sig*multiq2[(i+3)&3] * 0.0833333333333f;
      yiq->q=dp[24] = (dp[16+5] + dp[16+0]
                       +3.0f*(dp[16+4] + dp[16+1])
                       +4.0f*(dp[16+3] + dp[16+2])
                       -0.3333333333f * dp[24+2]);
    }
  } else {
    for (i=start, yiq=it_yiq+start; i<end; i++, yiq++) {
      yiq->i = yiq->q = 0.0f;
    }
  }
}

void
analogtv_setup_teletext(analogtv_input *input)
{
  int x,y;
  int teletext=ANALOGTV_BLACK_LEVEL;

  /* Teletext goes in line 21. But I suspect there are other things
     in the vertical retrace interval */

  for (y=19; y<22; y++) {
    for (x=ANALOGTV_PIC_START; x<ANALOGTV_PIC_END; x++) {
      if ((x&7)==0) {
        teletext=(random()&1) ? ANALOGTV_WHITE_LEVEL : ANALOGTV_BLACK_LEVEL;
      }
      input->signal[y][x]=teletext;
    }
  }
}

void
analogtv_setup_frame(analogtv *it)
{
  /*  int i,x,y;*/

  it->redraw_all=0;

  if (it->flutter_horiz_desync) {
    /* Horizontal sync during vertical sync instability. */
    it->horiz_desync += (float)(-0.10*(it->horiz_desync-3.0) +
      ((int)(random()&0xff)-0x80) *
      ((int)(random()&0xff)-0x80) *
      ((int)(random()&0xff)-0x80) * 0.000001);
  }

  /* it wasn't used
  for (i=0; i<ANALOGTV_V; i++) {
    it->hashnoise_times[i]=0;
  }
  */

  /* let's leave it to process shrinkpulse */
  if (it->hashnoise_enable && !it->hashnoise_on) {
    if (random()%10000==0) {
      it->hashnoise_on=1;
      it->shrinkpulse=random()%ANALOGTV_V;
    }
  }
  if (random()%1000==0) {
    it->hashnoise_on=0;
  }

#if 0  /* never used */
  if (it->hashnoise_on) {
    it->hashnoise_rpm += (15000.0 - it->hashnoise_rpm)*0.05 +
      ((int)(random()%2000)-1000)*0.1;
  } else {
    it->hashnoise_rpm -= 100 + 0.01*it->hashnoise_rpm;
    if (it->hashnoise_rpm<0.0) it->hashnoise_rpm=0.0;
  }
  if (it->hashnoise_rpm > 0.0) {
    int hni;
    double hni_double;
    int hnc=it->hashnoise_counter; /* in 24.8 format */

    /* Convert rpm of a 16-pole motor into dots in 24.8 format */
    hni_double = ANALOGTV_V * ANALOGTV_H * 256.0 /
                (it->hashnoise_rpm * 16.0 / 60.0 / 60.0);
    hni = (hni_double <= INT_MAX) ? (int)hni_double : INT_MAX;

    while (hnc < (ANALOGTV_V * ANALOGTV_H)<<8) {
      y=(hnc>>8)/ANALOGTV_H;
      x=(hnc>>8)%ANALOGTV_H;

      if (x>0 && x<ANALOGTV_H - ANALOGTV_HASHNOISE_LEN) {
        it->hashnoise_times[y]=x;
      }
      /* hnc += hni + (int)(random()%65536)-32768; */
      {
        hnc += (int)(random()%65536)-32768;
        if ((hnc >= 0) && (INT_MAX - hnc < hni)) break;
        hnc += hni;
      }
    }
  }
#endif /* 0 */

/*    hnc -= (ANALOGTV_V * ANALOGTV_H)<<8;*/


  if (it->rx_signal_level != 0.0)
    it->agclevel = (float)(1.0/it->rx_signal_level);


#ifdef DEBUG2
  printf("filter: ");
  for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
    printf(" %0.3f",it->ghostfir[i]);
  }
  printf(" siglevel=%g agc=%g\n", siglevel, it->agclevel);
#endif
}

PROTO_DLL void
analogtv_setup_sync(analogtv_input *input, int do_cb, int do_ssavi)
{
  int i,lineno,vsync;
  signed char *sig;

  int synclevel = do_ssavi ? ANALOGTV_WHITE_LEVEL : ANALOGTV_SYNC_LEVEL;

  for (lineno=0; lineno<ANALOGTV_V; lineno++) {
    vsync=lineno>=3 && lineno<7;

    sig=input->signal[lineno];

    i=ANALOGTV_SYNC_START;
    if (vsync) {
      while (i<ANALOGTV_BP_START) sig[i++]=ANALOGTV_BLANK_LEVEL;
      while (i<ANALOGTV_H) sig[i++]=synclevel;
    } else {
      while (i<ANALOGTV_BP_START) sig[i++]=synclevel;
      while (i<ANALOGTV_PIC_START) sig[i++]=ANALOGTV_BLANK_LEVEL;
      while (i<ANALOGTV_FP_START) sig[i++]=ANALOGTV_BLACK_LEVEL;
    }
    while (i<ANALOGTV_H) sig[i++]=ANALOGTV_BLANK_LEVEL;

    if (do_cb) {
      /* 9 cycles of colorburst */
      for (i=ANALOGTV_CB_START; i<ANALOGTV_CB_START+36; i+=4) {
        sig[i+1] += ANALOGTV_CB_LEVEL;
        sig[i+3] -= ANALOGTV_CB_LEVEL;
      }
    }
  }
}

static void
analogtv_sync(analogtv *it)
{
  int cur_hsync=it->cur_hsync;
  int cur_vsync=it->cur_vsync;
  int lineno = 0;
  int i,j;
  float osc,filt;
  float *sp;
  float cbfc=1.0f/128.0f;

/*  sp = it->rx_signal + lineno*ANALOGTV_H + cur_hsync;*/
  for (i=-32; i<32; i++) {
    lineno = (cur_vsync + i + ANALOGTV_V) % ANALOGTV_V;
    sp = it->rx_signal + lineno*ANALOGTV_H;
    filt=0.0f;
    for (j=0; j<ANALOGTV_H; j+=ANALOGTV_H/16) {
      filt += sp[j];
    }
    filt *= it->agclevel;

    osc = (float)(ANALOGTV_V+i)/(float)ANALOGTV_V;

    if (osc >= 1.05f+0.0002f * filt) break;
  }
  cur_vsync = (cur_vsync + i + ANALOGTV_V) % ANALOGTV_V;

  for (lineno=0; lineno<ANALOGTV_V; lineno++) {

    if (lineno>5 && lineno<ANALOGTV_V-3) { /* ignore vsync interval */
      unsigned lineno2 = (lineno + cur_vsync + ANALOGTV_V)%ANALOGTV_V;
      if (!lineno2) lineno2 = ANALOGTV_V;
      sp = it->rx_signal + lineno2*ANALOGTV_H + cur_hsync;
      for (i=-8; i<8; i++) {
        osc = (float)(ANALOGTV_H+i)/(float)ANALOGTV_H;
        filt=(sp[i-3]+sp[i-2]+sp[i-1]+sp[i]) * it->agclevel;

        if (osc >= 1.005f + 0.0001f*filt) break;
      }
      cur_hsync = (cur_hsync + i + ANALOGTV_H) % ANALOGTV_H;
    }

    it->line_hsync[lineno]=(cur_hsync + ANALOGTV_PIC_START +
                            ANALOGTV_H) % ANALOGTV_H;

    /* Now look for the colorburst, which is a few cycles after the H
       sync pulse, and store its phase.
       The colorburst is 9 cycles long, and we look at the middle 5
       cycles.
    */

    if (lineno>15) {
      sp = it->rx_signal + lineno*ANALOGTV_H + (cur_hsync&~3);
      for (i=ANALOGTV_CB_START+8; i<ANALOGTV_CB_START+36-8; i++) {
        it->cb_phase[i&3] = it->cb_phase[i&3]*(1.0f-cbfc) +
          sp[i]*it->agclevel*cbfc;
      }
    }

    {
      float tot=0.1f;
      float cbgain;

      for (i=0; i<4; i++) {
        tot += (float)(it->cb_phase[i]*it->cb_phase[i]);
      }
      cbgain = 32.0f/sqrtf(tot);

      for (i=0; i<4; i++) {
        it->line_cb_phase[lineno][i]=it->cb_phase[i]*cbgain;
      }
    }

#ifdef DEBUG
    if (0) printf("hs=%d cb=[%0.3f %0.3f %0.3f %0.3f]\n",
                  cur_hsync,
                  it->cb_phase[0], it->cb_phase[1],
                  it->cb_phase[2], it->cb_phase[3]);
#endif

    /* if (random()%2000==0) cur_hsync=random()%ANALOGTV_H; */
  }

  it->cur_hsync = cur_hsync;
  it->cur_vsync = cur_vsync;
}

static double
analogtv_levelmult(const analogtv *it, int level)
{
  static const double levelfac[3]={-7.5, 5.5, 24.5};
  return (40.0 + levelfac[level]*puramp(it, 3.0, 6.0, 1.0))/256.0;
}

static int
analogtv_level(const analogtv *it, int y, int ytop, int ybot)
{
  int level;
  if (ybot-ytop>=7) {
    if (y==ytop || y==ybot-1) level=0;
    else if (y==ytop+1 || y==ybot-2) level=1;
    else level=2;
  }
  else if (ybot-ytop>=5) {
    if (y==ytop || y==ybot-1) level=0;
    else level=2;
  }
  else if (ybot-ytop>=3) {
    if (y==ytop) level=0;
    else level=2;
  }
  else {
    level=2;
  }
  return level;
}

/*
  The point of this stuff is to ensure that when useheight is not a
  multiple of VISLINES so that TV scan lines map to different numbers
  of vertical screen pixels, the total brightness of each scan line
  remains the same.
  ANALOGTV_MAX_LINEHEIGHT corresponds to 2400 vertical pixels, beyond which
  it interpolates extra black lines.
 */

static void
analogtv_setup_levels(analogtv *it, double avgheight)
{
  int i,height;
  static const double levelfac[3]={-7.5, 5.5, 24.5};

  for (height=0; height<avgheight+2.0 && height<=ANALOGTV_MAX_LINEHEIGHT; height++) {

    for (i=0; i<height; i++) {
      it->leveltable[height][i].index = 2;
    }
    
    if (avgheight>=3) {
      it->leveltable[height][0].index=0;
    }
    if (avgheight>=5) {
      if (height >= 1) it->leveltable[height][height-1].index=0;
    }
    if (avgheight>=7) {
      it->leveltable[height][1].index=1;
      if (height >= 2) it->leveltable[height][height-2].index=1;
    }

    for (i=0; i<height; i++) {
      it->leveltable[height][i].value = 
        (40.0 + levelfac[it->leveltable[height][i].index]*puramp(it, 3.0, 6.0, 1.0)) / 256.0;
    }

  }
}

static void rnd_combine(unsigned *a0, unsigned *c0, unsigned a1, unsigned c1)
{
  *a0 = (*a0 * a1) & 0xffffffffu;
  *c0 = (c1 + a1 * *c0) & 0xffffffffu;
}

static void rnd_seek_ac(unsigned *a, unsigned *c, unsigned dist)
{
  unsigned int a1 = *a, c1 = *c;
  *a = 1, *c = 0;

  while(dist)
  {
    if(dist & 1)
      rnd_combine(a, c, a1, c1);
    dist >>= 1;
    rnd_combine(&a1, &c1, a1, c1);
  }
}

static unsigned int rnd_seek(unsigned a, unsigned c, unsigned rnd, unsigned dist)
{
  rnd_seek_ac(&a, &c, dist);
  return a * rnd + c;
}

static void analogtv_init_signal(const analogtv *it, double noiselevel, unsigned start, unsigned end)
{
  float *ps=it->rx_signal + start;
  float *pe=it->rx_signal + end;
  float *p=ps;
  unsigned int fastrnd=rnd_seek(FASTRND_A, FASTRND_C, it->random0, start);
  unsigned int fastrnd_offset;
  float nm1,nm2;
  float noisemul = (float)(sqrt(noiselevel*150)/(float)0x7fffffff);

  fastrnd_offset = fastrnd - 0x7fffffff;
  nm1 = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * noisemul;
  while (p != pe) {
    nm2=nm1;
    fastrnd = (fastrnd*FASTRND_A+FASTRND_C) & 0xffffffffu;
    fastrnd_offset = fastrnd - 0x7fffffff;
    nm1 = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * noisemul;
    *p++ = nm1*nm2;
  }
}

static void analogtv_add_signal(const analogtv *it, const analogtv_reception *rec, unsigned start, unsigned end, int ec)
{
  analogtv_input *inp=rec->input;
  float *ps=it->rx_signal + start;
  float *pe=it->rx_signal + end;
  float *p=ps;
  signed char *ss=&inp->signal[0][0];
  signed char *se=&inp->signal[0][0] + ANALOGTV_SIGNAL_LEN;
  signed char *s=ss + ((start + (unsigned)rec->ofs) % ANALOGTV_SIGNAL_LEN);
  signed char *s2;
  int i;
  float level=(float)(rec->level);
  float hfloss=(float)(rec->hfloss);
  unsigned int fastrnd=rnd_seek(FASTRND_A, FASTRND_C, it->random1, start);
  float dp[5];

  const float noise_decay = 0.99995f;
  float noise_ampl = (float)(1.3f * powf(noise_decay, (float)start));

  if (ec > end)
    ec = end;

  /* assert((se-ss)%4==0 && (se-s)%4==0); */

  for (i = start; i < ec; i++) { /* Sometimes start > ec. */

    /* Do a big noisy transition. We can make the transition noise of
       high constant strength regardless of signal strength.

       There are two separate state machines. here, One is the noise
       process and the other is the

       We don't bother with the FIR filter here
    */

    float sig0=(float)s[0];
    unsigned int fastrnd_offset = fastrnd - 0x7fffffff;
    float noise = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * (50.0f/(float)0x7fffffff);
    fastrnd = (fastrnd*FASTRND_A+FASTRND_C) & 0xffffffffu;

    p[0] += sig0 * level * (1.0f - noise_ampl) + noise * noise_ampl;

    noise_ampl *= noise_decay;

    p++;
    s++;
    if (s>=se) s=ss;
  }

  dp[0]=0.0;
  s2 = s;
  for (i=1; i<5; i++) {
    s2 -= 4;
    if (s2 < ss)
      s2 += ANALOGTV_SIGNAL_LEN;
    dp[i] = (float)((int)s2[0]+(int)s2[1]+(int)s2[2]+(int)s2[3]);
  }

  assert(p <= pe);
  assert(!((pe - p) % 4));
  while (p != pe) {
    float sig0,sig1,sig2,sig3,sigr;

    sig0=(float)s[0];
    sig1=(float)s[1];
    sig2=(float)s[2];
    sig3=(float)s[3];

    dp[0]=sig0+sig1+sig2+sig3;

    /* Get the video out signal, and add some ghosting, typical of RF
       monitor cables. This corresponds to a pretty long cable, but
       looks right to me.
    */

    sigr=(float)((dp[1]*rec->ghostfir[0] + dp[2]*rec->ghostfir[1] +
          dp[3]*rec->ghostfir[2] + dp[4]*rec->ghostfir[3]));
    dp[4]=dp[3]; dp[3]=dp[2]; dp[2]=dp[1]; dp[1]=dp[0];

    p[0] += (sig0+sigr + sig2*hfloss) * level;
    p[1] += (sig1+sigr + sig3*hfloss) * level;
    p[2] += (sig2+sigr + sig0*hfloss) * level;
    p[3] += (sig3+sigr + sig1*hfloss) * level;

    p += 4;
    s += 4;
    if (s>=se) s = ss + (s-se);
  }

  assert(p == pe);
}

static void analogtv_thread_add_signals(void *thread_raw)
{
  const analogtv_thread *thread = (analogtv_thread *)thread_raw;
  const analogtv *it = thread->it;
  unsigned i, j;
  unsigned subtotal_end;
  
  unsigned start = thread->signal_start;
  while(start != thread->signal_end)
  {
    float *p;
    
    /* Work on 8 KB blocks; these should fit in L1. */
    /* (Though it doesn't seem to help much on my system.) */
    unsigned end = start + 2048;
    if(end > thread->signal_end)
      end = thread->signal_end;

    analogtv_init_signal (it, it->noiselevel, start, end);

    for (i = 0; i != it->rec_count; ++i) {
      analogtv_add_signal (it, it->recs[i], start, end,
                           !i ? it->channel_change_cycles : 0);
    }

    assert (!(start % ANALOGTV_SUBTOTAL_LEN));
    assert (!(end % ANALOGTV_SUBTOTAL_LEN));

    p = it->rx_signal + start;
    subtotal_end = end / ANALOGTV_SUBTOTAL_LEN;
    for (i = start / ANALOGTV_SUBTOTAL_LEN; i != subtotal_end; ++i) {
      float sum = p[0];
      for (j = 1; j != ANALOGTV_SUBTOTAL_LEN; ++j)
        sum += p[j];
      it->signal_subtotals[i] = sum;
      p += ANALOGTV_SUBTOTAL_LEN;
    }
    
    start = end;
  }
}

static int analogtv_get_line(const analogtv *it, int lineno, int *slineno,
                             int *ytop, int *ybot, unsigned *signal_offset)
{
  *slineno=lineno-ANALOGTV_TOP;
  *ytop=(int)((*slineno*it->useheight/ANALOGTV_VISLINES -
                  it->useheight/2)*it->puheight) + it->useheight/2;
  *ybot=(int)(((*slineno+1)*it->useheight/ANALOGTV_VISLINES -
                  it->useheight/2)*it->puheight) + it->useheight/2;
#if 0
  int linesig=analogtv_line_signature(input,lineno)
    + it->hashnoise_times[lineno];
#endif
  *signal_offset = ((lineno+it->cur_vsync+ANALOGTV_V) % ANALOGTV_V) * ANALOGTV_H +
                    it->line_hsync[lineno];

  if (*ytop==*ybot) return 0;
  if (*ybot<0 || *ytop>it->useheight) return 0;
  if (*ytop<0) *ytop=0;
  if (*ybot>it->useheight) *ybot=it->useheight;

  if (*ybot > *ytop+ANALOGTV_MAX_LINEHEIGHT) *ybot=*ytop+ANALOGTV_MAX_LINEHEIGHT;
  return 1;
}

static void
analogtv_blast_imagerow(const analogtv *it,
                        float *rgbf, float *rgbf_end,
                        int ytop, int ybot)
{
  int i,j,x,y;
  float *rpf;
  char *level_copyfrom[3];
  int xrepl=it->xrepl;
  unsigned lineheight = ybot - ytop;
  if (lineheight > ANALOGTV_MAX_LINEHEIGHT) lineheight = ANALOGTV_MAX_LINEHEIGHT;
  for (i=0; i<3; i++) level_copyfrom[i]=NULL;

#ifdef WIN32
  i = 0;
  j = 0;
  int j_pix;
  for (y = ytop; y < ybot; y++) {
	  unsigned line = y - ytop;
	  float levelmult = (float)(it->leveltable[lineheight][line].value);
	  for (x = 0, rpf = rgbf; rpf != rgbf_end; x++, rpf += 3) {
      /* Grab the color for this "pixel" from levels. */
		  int ntscri = (int)(rpf[0] * levelmult);
		  int ntscgi = (int)(rpf[1] * levelmult);
		  int ntscbi = (int)(rpf[2] * levelmult);
		  if (ntscri >= ANALOGTV_CV_MAX) ntscri = ANALOGTV_CV_MAX - 1;
		  if (ntscgi >= ANALOGTV_CV_MAX) ntscgi = ANALOGTV_CV_MAX - 1;
		  if (ntscbi >= ANALOGTV_CV_MAX) ntscbi = ANALOGTV_CV_MAX - 1;
          
      /* The "pixel" may be multiple real pixels wide, up to xrepl. */
      for (j = 0; j < xrepl; j += 1) {
        j_pix = j * BM_PIXEL_WIDTH;
        bitmap_blast_data[i + j_pix] = (it->blue_values[ntscbi] >> it->blue_shift);
        bitmap_blast_data[i + j_pix + 1] = (it->green_values[ntscgi] >> it->green_shift);
        bitmap_blast_data[i + j_pix + 2] = it->red_values[ntscri];
      }

      /* Move on to the next "pixel". */
      i += (xrepl * BM_PIXEL_WIDTH);
	  }
  }

  /* Blast this scanline to the buffer. */
  SetDIBitsToDevice(bitmap_blast_hdcMem, 0, ytop, it->usewidth, 3, 0, 0, 0, 3, bitmap_blast_data, &bitmap_blast_info, DIB_RGB_COLORS);
#elif defined X11
  for (y=ytop; y<ybot; y++) {
    char *rowdata=it->image->data + y*it->image->bytes_per_line;
    unsigned line = y-ytop;

    int level=it->leveltable[lineheight][line].index;
    float levelmult=it->leveltable[lineheight][line].value;

    /* Fast special cases to avoid the slow XPutPixel. Ugh. It goes to show
       why standard graphics sw has to be fast, or else people will have to
       work around it and risk incompatibility. The quickdraw folks
       understood this. The other answer would be for X11 to have fewer
       formats for bitm.. oh, never mind. If neither of these cases work
       (they probably cover 99% of setups) it falls back on the Xlib
       routines. */

    if (level_copyfrom[level]) {
      memcpy(rowdata, level_copyfrom[level], it->image->bytes_per_line);
    }
    else {
      level_copyfrom[level] = rowdata;

      if (0) {
      }
      else if (it->image->format==ZPixmap &&
               it->image->bits_per_pixel==32 &&
               sizeof(unsigned int)==4 &&
               it->image->byte_order==localbyteorder) {
        /* int is more likely to be 32 bits than long */
        unsigned int *pixelptr=(unsigned int *)rowdata;
        unsigned int pix;

        for (rpf=rgbf; rpf!=rgbf_end; rpf+=3) {
          int ntscri=rpf[0]*levelmult;
          int ntscgi=rpf[1]*levelmult;
          int ntscbi=rpf[2]*levelmult;
          if (ntscri>=ANALOGTV_CV_MAX) ntscri=ANALOGTV_CV_MAX-1;
          if (ntscgi>=ANALOGTV_CV_MAX) ntscgi=ANALOGTV_CV_MAX-1;
          if (ntscbi>=ANALOGTV_CV_MAX) ntscbi=ANALOGTV_CV_MAX-1;
          //for (j = 0; j < PIXEL_DRAW_WIDTH; j += 1) {
            pix = (it->red_values[ntscri] |
                   it->green_values[ntscgi] |
                   it->blue_values[ntscbi]);
            pixelptr[0] = pix;
            if (xrepl>=2) {
              pixelptr[1] = pix;
              if (xrepl>=3) pixelptr[2] = pix;
            }
            pixelptr+=xrepl;
          //}
        }
      }
      else if (it->image->format==ZPixmap &&
               it->image->bits_per_pixel==16 &&
               sizeof(unsigned short)==2 &&
               float_extraction_works &&
               it->image->byte_order==localbyteorder) {
        unsigned short *pixelptr=(unsigned short *)rowdata;
        float r2,g2,b2;
        float_extract_t r1,g1,b1;
        unsigned short pix;

        for (rpf=rgbf; rpf!=rgbf_end; rpf+=3) {
          r2=rpf[0]; g2=rpf[1]; b2=rpf[2];
          r1.f=r2 * levelmult+float_low8_ofs;
          g1.f=g2 * levelmult+float_low8_ofs;
          b1.f=b2 * levelmult+float_low8_ofs;
          pix = (it->red_values[r1.i & 0x3ff] |
                 it->green_values[g1.i & 0x3ff] |
                 it->blue_values[b1.i & 0x3ff]);
          pixelptr[0] = pix;
          if (xrepl>=2) {
            pixelptr[1] = pix;
            if (xrepl>=3) pixelptr[2] = pix;
          }
          pixelptr+=xrepl;
        }
      }
      else if (it->image->format==ZPixmap &&
               it->image->bits_per_pixel==16 &&
               sizeof(unsigned short)==2 &&
               it->image->byte_order==localbyteorder) {
        unsigned short *pixelptr=(unsigned short *)rowdata;
        unsigned short pix;

        for (rpf=rgbf; rpf!=rgbf_end; rpf+=3) {
          int r1=rpf[0] * levelmult;
          int g1=rpf[1] * levelmult;
          int b1=rpf[2] * levelmult;
          if (r1>=ANALOGTV_CV_MAX) r1=ANALOGTV_CV_MAX-1;
          if (g1>=ANALOGTV_CV_MAX) g1=ANALOGTV_CV_MAX-1;
          if (b1>=ANALOGTV_CV_MAX) b1=ANALOGTV_CV_MAX-1;
          pix = it->red_values[r1] | it->green_values[g1] | it->blue_values[b1];
          pixelptr[0] = pix;
          if (xrepl>=2) {
            pixelptr[1] = pix;
            if (xrepl>=3) pixelptr[2] = pix;
          }
          pixelptr+=xrepl;
        }
      }
      else {
        for (x=0, rpf=rgbf; rpf!=rgbf_end ; x++, rpf+=3) {
          int ntscri=rpf[0]*levelmult;
          int ntscgi=rpf[1]*levelmult;
          int ntscbi=rpf[2]*levelmult;
          if (ntscri>=ANALOGTV_CV_MAX) ntscri=ANALOGTV_CV_MAX-1;
          if (ntscgi>=ANALOGTV_CV_MAX) ntscgi=ANALOGTV_CV_MAX-1;
          if (ntscbi>=ANALOGTV_CV_MAX) ntscbi=ANALOGTV_CV_MAX-1;
          for (j=0; j<xrepl; j++) {
            XPutPixel(it->image, x*xrepl + j, y,
                      it->red_values[ntscri] | it->green_values[ntscgi] |
                      it->blue_values[ntscbi]);
          }
        }
      }
    }
  }
#endif
}

static void analogtv_thread_draw_lines(void *thread_raw)
{
  const analogtv_thread *thread = (analogtv_thread *)thread_raw;
  const analogtv *it = thread->it;

  int lineno;

  float *raw_rgb_start;
  float *raw_rgb_end;
  raw_rgb_start=(float *)calloc(it->subwidth*3, sizeof(float));

  if (! raw_rgb_start) return;

  raw_rgb_end=raw_rgb_start+3*it->subwidth;

#ifdef WIN32
  HDC dpy = GetDC(it->window);
  bitmap_blast_data = calloc(bitmap_blast_bytes, sizeof(byte));
  bitmap_blast_hdcMem = CreateCompatibleDC(dpy);
  SelectObject(bitmap_blast_hdcMem, it->image);
#endif

  for (lineno=ANALOGTV_TOP + thread->thread_id;
       lineno<ANALOGTV_BOT;
       lineno += it->threads.count) {
    int i,x,y;
#ifndef WIN32
    int j;
#endif

    int slineno, ytop, ybot;
    unsigned signal_offset;

    const float *signal;

    int scanstart_i,scanend_i,squishright_i,squishdiv,pixrate;
    float *rgb_start, *rgb_end;
    float pixbright;
    int pixmultinc;

    float *rrp;

    struct analogtv_yiq_s yiq[ANALOGTV_PIC_LEN+10];

    if (! analogtv_get_line(it, lineno, &slineno, &ytop, &ybot,
        &signal_offset))
      continue;

    signal = it->rx_signal + signal_offset;

    {

      float bloomthisrow,shiftthisrow;
      float viswidth,middle;
      float scanwidth;
      int scw,scl,scr;

      bloomthisrow = -10.0f * it->crtload[lineno];
      if (bloomthisrow<-10.0f) bloomthisrow=-10.0f;
      if (bloomthisrow>2.0f) bloomthisrow=2.0f;
      if (slineno<16) {
        shiftthisrow=it->horiz_desync * (expf(-0.17f*slineno) *
                                         (0.7f+cosf(slineno*0.6f)));
      } else {
        shiftthisrow=0.0f;
      }

      viswidth=ANALOGTV_PIC_LEN * 0.79f - 5.0f*bloomthisrow;
      middle=ANALOGTV_PIC_LEN/2 - shiftthisrow;

      scanwidth=it->width_control * puramp(it, 0.5f, 0.3f, 1.0f);

      scw=(int)(it->subwidth*scanwidth);
      if (scw>it->subwidth) scw=it->usewidth;
      scl=(int)(it->subwidth/2 - scw/2);
      scr=(int)(it->subwidth/2 + scw/2);

      pixrate=(int)((int)((viswidth*65536.0f*1.0f)/it->subwidth)/scanwidth);
      scanstart_i=(int)((middle-viswidth*0.5f)*65536.0f);
      scanend_i=(ANALOGTV_PIC_LEN-1)*65536;
      squishright_i=(int)((middle+viswidth*(0.25f + 0.25f*puramp(it, 2.0f, 0.0f, 1.1f)
                                            - it->squish_control)) *65536.0f);
      squishdiv=it->subwidth/15;

      rgb_start=raw_rgb_start+scl*3;
      rgb_end=raw_rgb_start+scr*3;

      assert(scanstart_i>=0);

#ifdef DEBUG
      if (0) printf("scan %d: %0.3f %0.3f %0.3f scl=%d scr=%d scw=%d\n",
                    lineno,
                    scanstart_i/65536.0f,
                    squishright_i/65536.0f,
                    scanend_i/65536.0f,
                    scl,scr,scw);
#endif
    }

    if (it->use_cmap) {
      for (y=ytop; y<ybot; y++) {
        int level=analogtv_level(it, y, ytop, ybot);
        float levelmult=(float)analogtv_levelmult(it, level);
        float levelmult_y = levelmult * it->contrast_control
          * puramp(it, 1.0f, 0.0f, 1.0f) / (0.5f+0.5f*it->puheight) * 0.070f;
        float levelmult_iq = levelmult * 0.090f;

        analogtv_ntsc_to_yiq(it, lineno, signal,
                             (scanstart_i>>16)-10, (scanend_i>>16)+10, yiq);
        pixmultinc=pixrate;

        x=0;
        i=scanstart_i;

#ifdef X11
        while (i<0 && x<it->usewidth) {
          XPutPixel(it->image, x, y, it->colors[0]);
          i+=pixmultinc;
          x++;
        }
#endif
        while (i<scanend_i && x<it->usewidth) {
          float pixfrac=(i&0xffff)/65536.0f;
          float invpixfrac=(1.0f-pixfrac);
          int pati=i>>16;
          int yli,ili,qli,cmi;

          float interpy=(yiq[pati].y*invpixfrac
                         + yiq[pati+1].y*pixfrac) * levelmult_y;
          float interpi=(yiq[pati].i*invpixfrac
                         + yiq[pati+1].i*pixfrac) * levelmult_iq;
          float interpq=(yiq[pati].q*invpixfrac
                         + yiq[pati+1].q*pixfrac) * levelmult_iq;

          yli = (int)(interpy * it->cmap_y_levels);
          ili = (int)((interpi+0.5f) * it->cmap_i_levels);
          qli = (int)((interpq+0.5f) * it->cmap_q_levels);
          if (yli<0) yli=0;
          if (yli>=it->cmap_y_levels) yli=it->cmap_y_levels-1;
          if (ili<0) ili=0;
          if (ili>=it->cmap_i_levels) ili=it->cmap_i_levels-1;
          if (qli<0) qli=0;
          if (qli>=it->cmap_q_levels) qli=it->cmap_q_levels-1;

          cmi=qli + it->cmap_i_levels*(ili + it->cmap_q_levels*yli);

#ifdef DEBUG
          if ((random()%65536)==0) {
            printf("%0.3f %0.3f %0.3f => %d %d %d => %d\n",
                   interpy, interpi, interpq,
                   yli, ili, qli,
                   cmi);
          }
#endif

#ifdef X11
          for (j=0; j<it->xrepl; j++) {
            XPutPixel(it->image, x, y,
                      it->colors[cmi]);
            x++;
          }
#endif
          if (i >= squishright_i) {
            pixmultinc += pixmultinc/squishdiv;
          }
          i+=pixmultinc;
        }
#if defined X11
        while (x<it->usewidth) {
          XPutPixel(it->image, x, y, it->colors[0]);
		  x++;
		}
#endif
      }
    }
    else {
      analogtv_ntsc_to_yiq(it, lineno, signal,
                           (scanstart_i>>16)-10, (scanend_i>>16)+10, yiq);

      pixbright=it->contrast_control * puramp(it, 1.0f, 0.0f, 1.0f)
        / (0.5f+0.5f*it->puheight) * 1024.0f/100.0f;
      pixmultinc=pixrate;
      i=scanstart_i; rrp=rgb_start;
      while (i<0 && rrp!=rgb_end) {
        rrp[0]=rrp[1]=rrp[2]=0;
        i+=pixmultinc;
        rrp+=3;
      }
      while (i<scanend_i && rrp!=rgb_end) {
        float pixfrac=(i&0xffff)/65536.0f;
        float invpixfrac=1.0f-pixfrac;
        int pati=i>>16;
        float r,g,b;

        float interpy=(yiq[pati].y*invpixfrac + yiq[pati+1].y*pixfrac);
        float interpi=(yiq[pati].i*invpixfrac + yiq[pati+1].i*pixfrac);
        float interpq=(yiq[pati].q*invpixfrac + yiq[pati+1].q*pixfrac);

        /*
          According to the NTSC spec, Y,I,Q are generated as:

          y=0.30 r + 0.59 g + 0.11 b
          i=0.60 r - 0.28 g - 0.32 b
          q=0.21 r - 0.52 g + 0.31 b

          So if you invert the implied 3x3 matrix you get what standard
          televisions implement with a bunch of resistors (or directly in the
          CRT -- don't ask):

          r = y + 0.948 i + 0.624 q
          g = y - 0.276 i - 0.639 q
          b = y - 1.105 i + 1.729 q
        */

        r=(interpy + 0.948f*interpi + 0.624f*interpq) * pixbright;
        g=(interpy - 0.276f*interpi - 0.639f*interpq) * pixbright;
        b=(interpy - 1.105f*interpi + 1.729f*interpq) * pixbright;
        if (r<0.0f) r=0.0f;
        if (g<0.0f) g=0.0f;
        if (b<0.0f) b=0.0f;
        rrp[0]=r;
        rrp[1]=g;
        rrp[2]=b;

        if (i>=squishright_i) {
          pixmultinc += pixmultinc/squishdiv;
          pixbright += pixbright/squishdiv/2;
        }
        i+=pixmultinc;
        rrp+=3;
      }
      while (rrp != rgb_end) {
        rrp[0]=rrp[1]=rrp[2]=0.0f;
        rrp+=3;
      }

      analogtv_blast_imagerow(it, raw_rgb_start, raw_rgb_end,
                              ytop,ybot);
    }
  }

#ifdef WIN32
  free(bitmap_blast_data);
  DeleteObject(bitmap_blast_hdcMem);
#endif

  free(raw_rgb_start);
}

PROTO_DLL void
analogtv_draw(analogtv *it, double noiselevel,
              const analogtv_reception *const *recs, unsigned rec_count)
{
  int i,lineno;
  /*  int bigloadchange,drawcount;*/
  double baseload;
  int overall_top, overall_bot;

  /* AnalogTV isn't very interesting if there isn't enough RAM. */
  if (!it->image)
    return;

#ifdef WIN32
  HDC dpy = GetDC(it->window);
#endif

  it->rx_signal_level = noiselevel;
  for (i = 0; i != rec_count; ++i) {
    const analogtv_reception *rec = recs[i];
    double level = rec->level;
    analogtv_input *inp=rec->input;

    it->rx_signal_level =
      sqrt(it->rx_signal_level * it->rx_signal_level +
           (level * level * (1.0 + 4.0*(rec->ghostfir[0] + rec->ghostfir[1] +
                                        rec->ghostfir[2] + rec->ghostfir[3]))));

    /* duplicate the first line into the Nth line to ease wraparound computation */
    memcpy(inp->signal[ANALOGTV_V], inp->signal[0],
           ANALOGTV_H * sizeof(inp->signal[0][0]));
  }

  analogtv_setup_frame(it);
  analogtv_set_demod(it);

  it->random0 = random();
  it->random1 = random();
  it->noiselevel = noiselevel;
  it->recs = recs;
  it->rec_count = rec_count;
  threadpool_run(&it->threads, analogtv_thread_add_signals);
  threadpool_wait(&it->threads);

  it->channel_change_cycles=0;

  /* rx_signal has an extra 2 lines at the end, where we copy the
     first 2 lines so we can index into it while only worrying about
     wraparound on a per-line level */
  memcpy(&it->rx_signal[ANALOGTV_SIGNAL_LEN],
         &it->rx_signal[0],
         2*ANALOGTV_H*sizeof(it->rx_signal[0]));

  /* Repeat for signal_subtotals. */

  memcpy(&it->signal_subtotals[ANALOGTV_SIGNAL_LEN / ANALOGTV_SUBTOTAL_LEN],
         &it->signal_subtotals[0],
         (2*ANALOGTV_H/ANALOGTV_SUBTOTAL_LEN)*sizeof(it->signal_subtotals[0]));

  analogtv_sync(it); /* Requires the add_signals be complete. */

  baseload=0.5;
  /* if (it->hashnoise_on) baseload=0.5; */

  /*bigloadchange=1;
    drawcount=0;*/
  it->crtload[ANALOGTV_TOP-1]=(float)baseload;
  it->puheight = (float)(puramp(it, 2.0f, 1.0f, 1.3f) * it->height_control *
    (1.125 - 0.125*puramp(it, 2.0f, 2.0f, 1.1f)));

  analogtv_setup_levels(it, it->puheight * (double)it->useheight/(double)ANALOGTV_VISLINES);

  /* calculate tint once per frame */
  it->tint_i = (float)(-cos((103 + it->tint_control)*3.1415926/180));
  it->tint_q = (float)sin((103 + it->tint_control)*3.1415926/180);
  
  for (lineno=ANALOGTV_TOP; lineno<ANALOGTV_BOT; lineno++) {
    int slineno, ytop, ybot;
    unsigned signal_offset;
    if (! analogtv_get_line(it, lineno, &slineno, &ytop, &ybot, &signal_offset))
      continue;

    if (lineno==it->shrinkpulse) {
      baseload += 0.4;
      /*bigloadchange=1;*/
      it->shrinkpulse=-1;
    }

#if 0
    if (it->hashnoise_rpm>0.0 &&
        !(bigloadchange ||
          it->redraw_all ||
          (slineno<20 && it->flutter_horiz_desync) ||
          it->gaussiannoise_level>30 ||
          ((it->gaussiannoise_level>2.0 ||
            it->multipath) && random()%4) ||
          linesig != it->onscreen_signature[lineno])) {
      continue;
    }
    it->onscreen_signature[lineno] = linesig;
#endif
    /*    drawcount++;*/

    /*
      Interpolate the 600-dotclock line into however many horizontal
      screen pixels we're using, and convert to RGB.

      We add some 'bloom', variations in the horizontal scan width with
      the amount of brightness, extremely common on period TV sets. They
      had a single oscillator which generated both the horizontal scan and
      (during the horizontal retrace interval) the high voltage for the
      electron beam. More brightness meant more load on the oscillator,
      which caused an decrease in horizontal deflection. Look for
      (bloomthisrow).

      Also, the A2 did a bad job of generating horizontal sync pulses
      during the vertical blanking interval. This, and the fact that the
      horizontal frequency was a bit off meant that TVs usually went a bit
      out of sync during the vertical retrace, and the top of the screen
      would be bent a bit to the left or right. Look for (shiftthisrow).

      We also add a teeny bit of left overscan, just enough to be
      annoying, but you can still read the left column of text.

      We also simulate compression & brightening on the right side of the
      screen. Most TVs do this, but you don't notice because they overscan
      so it's off the right edge of the CRT. But the A2 video system used
      so much of the horizontal scan line that you had to crank the
      horizontal width down in order to not lose the right few characters,
      and you'd see the compression on the right edge. Associated with
      compression is brightening; since the electron beam was scanning
      slower, the same drive signal hit the phosphor harder. Look for
      (squishright_i) and (squishdiv).
    */

    {
      /* This used to be an int, I suspect by mistake. - Dave */
      float totsignal=0;
      float ncl/*,diff*/;
      unsigned frac;
      size_t end0, end1;
      const float *p;

      frac = signal_offset & (ANALOGTV_SUBTOTAL_LEN - 1);
      p = it->rx_signal + (signal_offset & ~(ANALOGTV_SUBTOTAL_LEN - 1));
      for (i=0; i != frac; i++) {
        totsignal -= p[i];
      }

      end0 = (signal_offset + ANALOGTV_PIC_LEN);

      end1 = end0 / ANALOGTV_SUBTOTAL_LEN;
      for (i=signal_offset / ANALOGTV_SUBTOTAL_LEN; i<end1; i++) {
        totsignal += it->signal_subtotals[i];
      }

      frac = end0 & (ANALOGTV_SUBTOTAL_LEN - 1);
      p = it->rx_signal + (end0 & ~(ANALOGTV_SUBTOTAL_LEN - 1));
      for (i=0; i != frac; i++) {
        totsignal += p[i];
      }

      totsignal *= it->agclevel;
      ncl = (float)(0.95f * it->crtload[lineno-1] +
        0.05f*(baseload +
               (totsignal-30000)/100000.0f +
               (slineno>184 ? (slineno-184)*(lineno-184)*0.001f * it->squeezebottom
                : 0.0f)));
      /*diff=ncl - it->crtload[lineno];*/
      /*bigloadchange = (diff>0.01 || diff<-0.01);*/
      it->crtload[lineno]=ncl;
    }
  }

  threadpool_run(&it->threads, analogtv_thread_draw_lines);
  threadpool_wait(&it->threads);

#if 0
  /* poor attempt at visible retrace */
  for (i=0; i<15; i++) {
    int ytop=(int)((i*it->useheight/15 -
                    it->useheight/2)*puheight) + it->useheight/2;
    int ybot=(int)(((i+1)*it->useheight/15 -
                    it->useheight/2)*puheight) + it->useheight/2;
    int div=it->usewidth*3/2;

    for (x=0; x<it->usewidth; x++) {
      y = ytop + (ybot-ytop)*x / div;
      if (y<0 || y>=it->useheight) continue;
      XPutPixel(it->image, x, y, 0xffffff);
    }
  }
#endif

  if (it->need_clear) {
    it->need_clear=0;
  }

  /*
    Subtle change: overall_bot was the bottom of the last scan line. Now it's
    the top of the next-after-the-last scan line. This is the same until
    the y-dimension is > 2400, note ANALOGTV_MAX_LINEHEIGHT.
  */

  overall_top=(int)(it->useheight*(1-it->puheight)/2);
  overall_bot=(int)(it->useheight*(1+it->puheight)/2);

  if (overall_top<0) overall_top=0;
  if (overall_bot>it->useheight) overall_bot=it->useheight;

#ifdef WIN32
  HBRUSH hbrBkgnd = GetStockObject(BLACK_BRUSH);
  RECT rcBmp;
  if (overall_top>0) {
	  rcBmp.left = it->screen_xo;
	  rcBmp.top = it->screen_yo;
	  rcBmp.right = it->usewidth;
	  rcBmp.bottom = overall_top;
  }
  FillRect(dpy, &rcBmp, hbrBkgnd);
#elif defined X11
  if (overall_top>0) {
    XClearArea(it->dpy, it->window,
               it->screen_xo, it->screen_yo,
               it->usewidth, overall_top, 0);
  }
  if (it->useheight > overall_bot) {
    XClearArea(it->dpy, it->window,
               it->screen_xo, it->screen_yo+overall_bot,
               it->usewidth, it->useheight-overall_bot, 0);
  }
#endif

  if (overall_bot > overall_top) {
    if (it->use_shm) {
#ifdef HAVE_XSHM_EXTENSION
      XShmPutImage(it->dpy, it->window, it->gc, it->image,
                   0, overall_top,
                   it->screen_xo, it->screen_yo+overall_top,
                   it->usewidth, overall_bot - overall_top,
                   False);
#endif
    } else {

        /* Draw the buffer to the screen. */
#ifdef WIN32
		BITMAP bitmap;
		HDC hdcMem = CreateCompatibleDC(dpy);
		SelectObject(hdcMem, it->image);

		GetObject(it->image, sizeof(bitmap), &bitmap);
		BitBlt(dpy, it->screen_xo, it->screen_yo, it->usewidth, it->useheight, hdcMem, 0, 0, SRCCOPY);

		DeleteDC(hdcMem);

		ReleaseDC(it->window, dpy);
#elif defined X11
      XPutImage(it->dpy, it->window, it->gc, it->image,
                0, overall_top,
                it->screen_xo, it->screen_yo+overall_top,
                it->usewidth, overall_bot - overall_top);
#endif
    }
  }

  /* TODO: Salvage and ifdef this FPS counter. */
#ifdef DEBUG
#if 0
  if (0) {
    struct timeval tv;
    double fps;
    char buf[256];
    gettimeofday(&tv,NULL);

    fps=1.0/((tv.tv_sec - it->last_display_time.tv_sec)
             + 0.000001*(tv.tv_usec - it->last_display_time.tv_usec));
    sprintf(buf, "FPS=%0.1f",fps);
    XDrawString(it->dpy, it->window, it->gc, 50, it->useheight*2/3,
                buf, strlen(buf));

    it->last_display_time=tv;
  }
#endif
#endif
}

PROTO_DLL analogtv_input *
analogtv_input_allocate()
{
  analogtv_input *ret=(analogtv_input *)calloc(1,sizeof(analogtv_input));

  return ret;
}

PROTO_DLL analogtv_reception *
analogtv_reception_allocate(float level, analogtv_input *input)
{
	analogtv_reception *ret = (analogtv_reception *)calloc(1, sizeof(analogtv_reception));

	ret->input = input;

	ret->level = level;
	ret->ofs = 0;
	ret->multipath = 0.0;

	return ret;
}

PROTO_DLL void
analogtv_reception_reallocate(analogtv_reception *rec, analogtv_input *input)
{
    free(rec->input);
    rec->input = input;
}

/*
  This takes a screen image and encodes it as a video camera would,
  including all the bandlimiting and YIQ modulation.
  This isn't especially tuned for speed.
*/

PROTO_DLL int
#ifdef WIN32
analogtv_load_ximage(analogtv *it, analogtv_input *input, HBITMAP pic_im)
#elif defined X11
analogtv_load_ximage(analogtv *it, analogtv_input *input, XImage *pic_im)
#elif defined ALLEGRO
analogtv_load_ximage(analogtv *it, analogtv_input *input, BITMAP *pic_im)
#endif
{
  int i,x,y;
  int img_w,img_h;
  int fyx[7],fyy[7];
  int fix[4],fiy[4];
  int fqx[4],fqy[4];
  XColor col1[ANALOGTV_PIC_LEN];
  XColor col2[ANALOGTV_PIC_LEN];
  int multiq[ANALOGTV_PIC_LEN+4];
  int y_overscan=5; /* overscan this much top and bottom */
  int y_scanlength=ANALOGTV_VISLINES+2*y_overscan;

#ifdef WIN32
  HDC dpy = GetDC(it->window);

  SIZE idimensions;
  GetBitmapDimensionEx(pic_im, &idimensions);
  img_w = idimensions.cx;
  img_h = idimensions.cy;
#elif defined X11
  img_w=pic_im->width;
  img_h=pic_im->height;
#endif

  for (i=0; i<ANALOGTV_PIC_LEN+4; i++) {
    double phase=90.0-90.0*i;
    double ampl=1.0;
    multiq[i]=(int)(-cos(3.1415926/180.0*(phase-303)) * 4096.0 * ampl);
  }

  for (y=0; y<y_scanlength; y++) {
    int picy1=(y*img_h)/y_scanlength;
    int picy2=(y*img_h + y_scanlength/2)/y_scanlength;

    for (x=0; x<ANALOGTV_PIC_LEN; x++) {
      int picx=(x*img_w)/ANALOGTV_PIC_LEN;
#ifdef WIN32
	  HDC hdcMem = CreateCompatibleDC(dpy);
	  SelectObject(hdcMem, pic_im);
	  
	  col1[x].pixel = GetPixel(hdcMem, picx, picy1);
	  col2[x].pixel = GetPixel(hdcMem, picx, picy2);

	  DeleteDC(hdcMem);
	  ReleaseDC(it->window, dpy);
#elif defined X11
      col1[x].pixel=XGetPixel(pic_im, picx, picy1);
      col2[x].pixel=XGetPixel(pic_im, picx, picy2);
#endif
    }
#ifdef X11
    XQueryColors(it->dpy, it->colormap, col1, ANALOGTV_PIC_LEN);
    XQueryColors(it->dpy, it->colormap, col2, ANALOGTV_PIC_LEN);
#endif

    for (i=0; i<7; i++) fyx[i]=fyy[i]=0;
    for (i=0; i<4; i++) fix[i]=fiy[i]=fqx[i]=fqy[i]=0;

    for (x=0; x<ANALOGTV_PIC_LEN; x++) {
      int rawy,rawi,rawq;
      int filty,filti,filtq;
      int composite;
      /* Compute YIQ as:
           y=0.30 r + 0.59 g + 0.11 b
           i=0.60 r - 0.28 g - 0.32 b
           q=0.21 r - 0.52 g + 0.31 b
          The coefficients below are in .4 format */

      rawy=( 5*col1[x].red + 11*col1[x].green + 2*col1[x].blue +
             5*col2[x].red + 11*col2[x].green + 2*col2[x].blue)>>7;
      rawi=(10*col1[x].red -  4*col1[x].green - 5*col1[x].blue +
            10*col2[x].red -  4*col2[x].green - 5*col2[x].blue)>>7;
      rawq=( 3*col1[x].red -  8*col1[x].green + 5*col1[x].blue +
             3*col2[x].red -  8*col2[x].green + 5*col2[x].blue)>>7;

      /* Filter y at with a 4-pole low-pass Butterworth filter at 3.5 MHz
         with an extra zero at 3.5 MHz, from
         mkfilter -Bu -Lp -o 4 -a 2.1428571429e-01 0 -Z 2.5e-01 -l */

      fyx[0] = fyx[1]; fyx[1] = fyx[2]; fyx[2] = fyx[3];
      fyx[3] = fyx[4]; fyx[4] = fyx[5]; fyx[5] = fyx[6];
      fyx[6] = (rawy * 1897) >> 16;
      fyy[0] = fyy[1]; fyy[1] = fyy[2]; fyy[2] = fyy[3];
      fyy[3] = fyy[4]; fyy[4] = fyy[5]; fyy[5] = fyy[6];
      fyy[6] = (fyx[0]+fyx[6]) + 4*(fyx[1]+fyx[5]) + 7*(fyx[2]+fyx[4]) + 8*fyx[3]
        + ((-151*fyy[2] + 8115*fyy[3] - 38312*fyy[4] + 36586*fyy[5]) >> 16);
      filty = fyy[6];

      /* Filter I at 1.5 MHz. 3 pole Butterworth from
         mkfilter -Bu -Lp -o 3 -a 1.0714285714e-01 0 */

      fix[0] = fix[1]; fix[1] = fix[2]; fix[2] = fix[3];
      fix[3] = (rawi * 1413) >> 16;
      fiy[0] = fiy[1]; fiy[1] = fiy[2]; fiy[2] = fiy[3];
      fiy[3] = (fix[0]+fix[3]) + 3*(fix[1]+fix[2])
        + ((16559*fiy[0] - 72008*fiy[1] + 109682*fiy[2]) >> 16);
      filti = fiy[3];

      /* Filter Q at 0.5 MHz. 3 pole Butterworth from
         mkfilter -Bu -Lp -o 3 -a 3.5714285714e-02 0 -l */

      fqx[0] = fqx[1]; fqx[1] = fqx[2]; fqx[2] = fqx[3];
      fqx[3] = (rawq * 75) >> 16;
      fqy[0] = fqy[1]; fqy[1] = fqy[2]; fqy[2] = fqy[3];
      fqy[3] = (fqx[0]+fqx[3]) + 3 * (fqx[1]+fqx[2])
        + ((2612*fqy[0] - 9007*fqy[1] + 10453 * fqy[2]) >> 12);
      filtq = fqy[3];


      composite = filty + ((multiq[x] * filti + multiq[x+3] * filtq)>>12);
      composite = ((composite*100)>>14) + ANALOGTV_BLACK_LEVEL;
      if (composite>125) composite=125;
      if (composite<0) composite=0;
      input->signal[y-y_overscan+ANALOGTV_TOP][x+ANALOGTV_PIC_START] = composite;
    }
  }

  return 1;
}

#if 0
void analogtv_channel_noise(analogtv_input *it, analogtv_input *s2)
{
  int x,y,newsig;
  int change=random()%ANALOGTV_V;
  unsigned int fastrnd=random();
  double hso=(int)(random()%1000)-500;
  int yofs=random()%ANALOGTV_V;
  int noise;

  for (y=change; y<ANALOGTV_V; y++) {
    int s2y=(y+yofs)%ANALOGTV_V;
    int filt=0;
    int noiselevel=60000 / (y-change+100);

    it->line_hsync[y] = s2->line_hsync[y] + (int)hso;
    hso *= 0.9;
    for (x=0; x<ANALOGTV_H; x++) {
      FASTRND;
      filt+= (-filt/16) + (int)(fastrnd&0xfff)-0x800;
      noise=(filt*noiselevel)>>16;
      newsig=s2->signal[s2y][x] + noise;
      if (newsig>120) newsig=120;
      if (newsig<0) newsig=0;
      it->signal[y][x]=newsig;
    }
  }
  s2->vsync=yofs;
}
#endif


#ifdef FIXME
/* add hash */
  if (it->hashnoise_times[lineno]) {
    int hnt=it->hashnoise_times[lineno] - input->line_hsync[lineno];

    if (hnt>=0 && hnt<ANALOGTV_PIC_LEN) {
      double maxampl=1.0;
      double cur=frand(150.0)-20.0;
      int len=random()%15+3;
      if (len > ANALOGTV_PIC_LEN-hnt) len=ANALOGTV_PIC_LEN-hnt;
      for (i=0; i<len; i++) {
        double sig=signal[hnt];

        sig += cur*maxampl;
        cur += frand(5.0)-5.0;
        maxampl = maxampl*0.9;

        signal[hnt]=sig;
        hnt++;
      }
    }
  }
#endif


PROTO_DLL void
analogtv_reception_update(analogtv_reception *rec)
{
  int i;

  if (rec->multipath > 0.0) {
    for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
      rec->ghostfir2[i] +=
        -(rec->ghostfir2[i]/16.0) + rec->multipath * (frand(0.02)-0.01);
    }
    if (random()%20==0) {
      rec->ghostfir2[random()%(ANALOGTV_GHOSTFIR_LEN)]
        = rec->multipath * (frand(0.08)-0.04);
    }
    for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
      rec->ghostfir[i] = 0.8*rec->ghostfir[i] + 0.2*rec->ghostfir2[i];
    }

    if (0) {
      rec->hfloss2 += -(rec->hfloss2/16.0) + rec->multipath * (frand(0.08)-0.04);
      rec->hfloss = 0.5*rec->hfloss + 0.5*rec->hfloss2;
    }

  } else {
    for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
      if (i >= ANALOGTV_GHOSTFIR_LEN / 2) {
        /* 
         * Let's make this a bit more readable/debugable. The original line was:
         *
         * rec->ghostfir[i] = ((i & 1) ? +0.04 : -0.08) / ANALOGTV_GHOSTFIR_LEN;
         */
        float divisor_addition = 0.0;
        if (i & 1) {
          rec->ghostfir[i] = 0.04;
        } else {
          rec->ghostfir[i] = -0.08;
        }
        rec->ghostfir[i] /= ANALOGTV_GHOSTFIR_LEN;
      } else {
        rec->ghostfir[i] = 0.0;
      }
    }
  }
}

#ifdef ANALOGTV_FONTS

/* jwz: since MacOS doesn't have "6x10", I dumped this font to an XBM...
 */

#include "images/6x10font.xbm"

void
#ifdef WIN32
analogtv_make_font(HWND window, analogtv_font *f,
#elif defined X11
analogtv_make_font(Display *dpy, Window window, analogtv_font *f,
#elif defined ALLEGRO
analogtv_make_font(analogtv_font *f,
#endif
                   int w, int h, char *fontname)
{
#ifdef WIN32
	// XXX
#elif defined X11
  int i;
  XFontStruct *font;
  Pixmap text_pm;
  GC gc;
  XGCValues gcv;
  XWindowAttributes xgwa;

  f->char_w = w;
  f->char_h = h;

  XGetWindowAttributes (dpy, window, &xgwa);

  if (fontname && !strcmp (fontname, "6x10")) {

    text_pm = XCreatePixmapFromBitmapData (dpy, window,
                                           (char *) font6x10_bits,
                                           font6x10_width,
                                           font6x10_height,
                                           1, 0, 1);
    f->text_im = XGetImage(dpy, text_pm, 0, 0, font6x10_width, font6x10_height,
                           1, XYPixmap);
    XFreePixmap(dpy, text_pm);

  } else if (fontname) {

    font = XLoadQueryFont (dpy, fontname);
    if (!font) {
      fprintf(stderr, "analogtv: can't load font %s\n", fontname);
      abort();
    }

    text_pm=XCreatePixmap(dpy, window, 256*f->char_w, f->char_h, 1);

    memset(&gcv, 0, sizeof(gcv));
    gcv.foreground=1;
    gcv.background=0;
    gcv.font=font->fid;
    gc=XCreateGC(dpy, text_pm, GCFont|GCBackground|GCForeground, &gcv);

    XSetForeground(dpy, gc, 0);
    XFillRectangle(dpy, text_pm, gc, 0, 0, 256*f->char_w, f->char_h);
    XSetForeground(dpy, gc, 1);
    for (i=0; i<256; i++) {
      char c=i;
      int x=f->char_w*i+1;
      int y=f->char_h*8/10;
      XDrawString(dpy, text_pm, gc, x, y, &c, 1);
    }
    f->text_im = XGetImage(dpy, text_pm, 0, 0, 256*f->char_w, f->char_h,
                           1, XYPixmap);
    XFreeGC(dpy, gc);
    XFreePixmap(dpy, text_pm);
  } else {
    f->text_im = XCreateImage(dpy, xgwa.visual, 1, XYPixmap, 0, 0,
                              256*f->char_w, f->char_h, 8, 0);
    f->text_im->data = (char *)calloc(f->text_im->height,
                                      f->text_im->bytes_per_line);

  }
  f->x_mult=4;
  f->y_mult=2;

#endif
}

PROTO_DLL int
analogtv_font_pixel(analogtv_font *f, int c, int x, int y)
{
  if (x<0 || x>=f->char_w) return 0;
  if (y<0 || y>=f->char_h) return 0;
  if (c<0 || c>=256) return 0;
#ifdef WIN32
  // XXX
  return 0;
#elif defined X11
  return XGetPixel(f->text_im, c*f->char_w + x, y) ? 1 : 0;
#endif
}

PROTO_DLL void
#ifdef WIN32
analogtv_font_set_pixel(analogtv_font *f, int c, int x, int y, int value, analogtv* it)
#else
analogtv_font_set_pixel(analogtv_font *f, int c, int x, int y, int value)
#endif
{
  if (x<0 || x>=f->char_w) return;
  if (y<0 || y>=f->char_h) return;
  if (c<0 || c>=256) return;

#ifdef WIN32
  HDC dpy = GetDC(it->window);
  HDC hdcMem = CreateCompatibleDC(dpy);
  SelectObject(hdcMem, f->text_im);

  bitmap_blit_pixel(hdcMem, c*f->char_w + x, y, value);

  DeleteDC(hdcMem);
  ReleaseDC(it->window, dpy);
#elif defined X11
  XPutPixel(f->text_im, c*f->char_w + x, y, value);
#endif
}

void
#ifdef WIN32
analogtv_font_set_char(analogtv_font *f, int c, char *s, analogtv* it)
#else
analogtv_font_set_char(analogtv_font *f, int c, char *s)
#endif
{
  int value,x,y;

  if (c<0 || c>=256) return;

  for (y=0; y<f->char_h; y++) {
    for (x=0; x<f->char_w; x++) {
      if (!*s) return;
      value=(*s==' ') ? 0 : 1;
#ifdef WIN32
      analogtv_font_set_pixel(f, c, x, y, value, it);
#else
	  analogtv_font_set_pixel(f, c, x, y, value);
#endif
      s++;
    }
  }
}

#endif

PROTO_DLL void
analogtv_lcp_to_ntsc(double luma, double chroma, double phase, int ntsc[4])
{
  int i;
  for (i=0; i<4; i++) {
    double w=90.0*i + phase;
    double val=luma + chroma * (cos(3.1415926/180.0*w));
    if (val<0.0) val=0.0;
    if (val>127.0) val=127.0;
    ntsc[i]=(int)val;
  }
}

PROTO_DLL void
analogtv_draw_solid(analogtv_input *input,
                    int left, int right, int top, int bot,
                    int ntsc[4])
{
  int x,y;

  if (right-left<4) right=left+4;
  if (bot-top<1) bot=top+1;

#ifdef SAFETY
  /* TODO: Do safety checks mathematically for performance. */
  if (ANALOGTV_TOP > top) {
    top = ANALOGTV_TOP;
  }
  if (ANALOGTV_BOT < bot) {
    bot = ANALOGTV_BOT;
  }
  if (ANALOGTV_VIS_START > left) {
    left = ANALOGTV_VIS_START;
  }
  if (ANALOGTV_VIS_END < right) {
      right = ANALOGTV_VIS_END;
  }
#endif

  for (y=top; y<bot; y++) {
    for (x=left; x<right; x++) {
#if 0
      if(
          ANALOGTV_VIS_START <= x && ANALOGTV_TOP <= y &&
          ANALOGTV_VIS_END > x && ANALOGTV_BOT > y
      ) {
#endif
        input->signal[y][x] = ntsc[x&3];
#if 0
      }
#endif
    }
  }
}


PROTO_DLL void
analogtv_draw_solid_rel_lcp(analogtv_input *input,
                            double left, double right, double top, double bot,
                            double luma, double chroma, double phase)
{
  int ntsc[4];

  int topi=(int)(ANALOGTV_TOP + ANALOGTV_VISLINES*top);
  int boti=(int)(ANALOGTV_TOP + ANALOGTV_VISLINES*bot);
  int lefti=(int)(ANALOGTV_VIS_START + ANALOGTV_VIS_LEN*left);
  int righti=(int)(ANALOGTV_VIS_START + ANALOGTV_VIS_LEN*right);

  analogtv_lcp_to_ntsc(luma, chroma, phase, ntsc);
  analogtv_draw_solid(input, lefti, righti, topi, boti, ntsc);
}

#ifdef ANALOGTV_FONTS

PROTO_DLL void
analogtv_draw_char(analogtv_input *input, analogtv_font *f,
                   int c, int x, int y, int ntsc[4])
{
  int yc,xc,ys,xs,pix;

  for (yc=0; yc<f->char_h; yc++) {
    for (ys=y + yc*f->y_mult; ys<y + (yc+1)*f->y_mult; ys++) {
      if (ys<0 || ys>=ANALOGTV_V) continue;

      for (xc=0; xc<f->char_w; xc++) {
        pix=analogtv_font_pixel(f, c, xc, yc);

        for (xs=x + xc*f->x_mult; xs<x + (xc+1)*f->x_mult; xs++) {
          if (xs<0 || xs>=ANALOGTV_H) continue;
          if (pix) {
            input->signal[ys][xs] = ntsc[xs&3];
          }
        }
      }
    }
  }
}

PROTO_DLL void
analogtv_draw_string(analogtv_input *input, analogtv_font *f,
                     char *s, int x, int y, int ntsc[4])
{
  while (*s) {
    analogtv_draw_char(input, f, *s, x, y, ntsc);
    x += f->char_w * 4;
    s++;
  }
}

PROTO_DLL void
analogtv_draw_string_centered(analogtv_input *input, analogtv_font *f,
                              char *s, int x, int y, int ntsc[4])
{
  int width=strlen(s) * f->char_w * 4;
  x -= width/2;

  analogtv_draw_string(input, f, s, x, y, ntsc);
}

#endif

static const char hextonib[128] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
                                   0, 10,11,12,13,14,15,0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 10,11,12,13,14,15,0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

enum {
  ANALOGTV_IMAGE_MASK_R = 0xff000000,
  ANALOGTV_IMAGE_OFFSET_R = 24,
  ANALOGTV_IMAGE_MASK_G = 0x00ff0000,
  ANALOGTV_IMAGE_OFFSET_G = 16,
  ANALOGTV_IMAGE_MASK_B = 0x0000ff00,
  ANALOGTV_IMAGE_OFFSET_B = 8,
};

/*
 * data should be a pointer to an array of unsigned ints sized as
 * width * height. The image will be drawn line by line from the top down,
 * with each int being used as 0xrrggbb00. This provides a common format to
 * to translate other images to.
 */
PROTO_DLL void
analogtv_draw_image(analogtv_input *input, unsigned int *data, int left, int top, unsigned imagew, unsigned imageh) {
  int x, y, tvx, tvy, i;
  int rawy, rawi, rawq;

  for (y = 0; y < imageh; y++) {
    tvy = y + top;
    if (tvy < ANALOGTV_TOP || tvy >= ANALOGTV_BOT) continue;

    for (x = 0; x < imagew; x++) {
      //int cbyte = ((unsigned char)line[x]) & 0xff;
      unsigned int cword = data[(y * imagew) + x];
      int ntsc[4];
      tvx = x * 4 + left;

      /* Only draw columns inside of the visible range. */
      if (tvx<ANALOGTV_VIS_START || tvx + 4>ANALOGTV_VIS_END) continue;

      /* Extract each column pixel and translate it into NTSC color. */
      unsigned int rawr = (cword & ANALOGTV_IMAGE_MASK_R) >> ANALOGTV_IMAGE_OFFSET_R;
      unsigned int rawg = (cword & ANALOGTV_IMAGE_MASK_G) >> ANALOGTV_IMAGE_OFFSET_G;
      unsigned int rawb = (cword & ANALOGTV_IMAGE_MASK_B) >> ANALOGTV_IMAGE_OFFSET_B;

#if 0
      rawy = (5 * rawr + 11 * rawg + 2 * rawb) / 64;
      rawi = (10 * rawr - 4 * rawg - 5 * rawb) / 64;
      rawq = (3 * rawr - 8 * rawg + 5 * rawb) / 64;

      ntsc[0] = rawy + rawq;
      ntsc[1] = rawy - rawi;
      ntsc[2] = rawy - rawq;
      ntsc[3] = rawy + rawi;

      for (i = 0; i < 4; i++) {
        if (ntsc[i] > ANALOGTV_WHITE_LEVEL) ntsc[i] = ANALOGTV_WHITE_LEVEL;
        if (ntsc[i] < ANALOGTV_BLACK_LEVEL) ntsc[i] = ANALOGTV_BLACK_LEVEL;
      }
#endif

      analogtv_rgb_to_ntsc(rawr, rawg, rawb, ntsc);

      /* Draw the image into the signal array. */
      input->signal[tvy][tvx + 0] = ntsc[(tvx + 0) & 3];
      input->signal[tvy][tvx + 1] = ntsc[(tvx + 1) & 3];
      input->signal[tvy][tvx + 2] = ntsc[(tvx + 2) & 3];
      input->signal[tvy][tvx + 3] = ntsc[(tvx + 3) & 3];
    }
  }
}

/*
  Much of this function was adapted from logo.c
 */
PROTO_DLL void
analogtv_draw_xpm(analogtv *tv, analogtv_input *input,
                  const char * const *xpm, int left, int top)
{
  int xpmw,xpmh;
  int x,y,tvx,tvy,i;
  int rawy,rawi,rawq;
  int ncolors, nbytes;
  char dummyc;
  struct {
    int r; int g; int b;
  } cmap[256];


  if (4 != sscanf ((const char *) *xpm,
                   "%d %d %d %d %c",
                   &xpmw, &xpmh, &ncolors, &nbytes, &dummyc))
    abort();
  if (ncolors < 1 || ncolors > 255)
    abort();
  if (nbytes != 1) /* a serious limitation */
    abort();
  xpm++;

  for (i = 0; i < ncolors; i++) {
    const char *line = *xpm;
    int colori = ((unsigned char)*line++)&0xff;
    while (*line)
      {
        int r, g, b;
        char which;
        while (*line == ' ' || *line == '\t')
          line++;
        which = *line++;
        if (which != 'c' && which != 'm')
          abort();
        while (*line == ' ' || *line == '\t')
          line++;
#ifdef WIN32
		if (!strncmp(line, "None", 4) && !strncmp(line, "none", 4))
#else
        if (!strncasecmp(line, "None", 4))
#endif
          {
            r = g = b = -1;
            line += 4;
          }
        else
          {
            if (*line == '#')
              line++;
            r = (hextonib[(int) line[0]] << 4) | hextonib[(int) line[1]];
            line += 2;
            g = (hextonib[(int) line[0]] << 4) | hextonib[(int) line[1]];
            line += 2;
            b = (hextonib[(int) line[0]] << 4) | hextonib[(int) line[1]];
            line += 2;
          }

        if (which == 'c')
          {
            cmap[colori].r = r;
            cmap[colori].g = g;
            cmap[colori].b = b;
          }
      }

    xpm++;
  }

  for (y=0; y<xpmh; y++) {
    const char *line = *xpm++;
    tvy=y+top;
    if (tvy<ANALOGTV_TOP || tvy>=ANALOGTV_BOT) continue;

    for (x=0; x<xpmw; x++) {
      int cbyte=((unsigned char)line[x])&0xff;
      int ntsc[4];
      tvx=x*4+left;
      if (tvx<ANALOGTV_PIC_START || tvx+4>ANALOGTV_PIC_END) continue;

      /*
      rawy=( 5*cmap[cbyte].r + 11*cmap[cbyte].g + 2*cmap[cbyte].b) / 64;
      rawi=(10*cmap[cbyte].r -  4*cmap[cbyte].g - 5*cmap[cbyte].b) / 64;
      rawq=( 3*cmap[cbyte].r -  8*cmap[cbyte].g + 5*cmap[cbyte].b) / 64;

      ntsc[0]=rawy+rawq;
      ntsc[1]=rawy-rawi;
      ntsc[2]=rawy-rawq;
      ntsc[3]=rawy+rawi;

      for (i=0; i<4; i++) {
        if (ntsc[i]>ANALOGTV_WHITE_LEVEL) ntsc[i]=ANALOGTV_WHITE_LEVEL;
        if (ntsc[i]<ANALOGTV_BLACK_LEVEL) ntsc[i]=ANALOGTV_BLACK_LEVEL;
      }
      */

      analogtv_rgb_to_ntsc(cmap[cbyte].r, cmap[cbyte].g, cmap[cbyte].b, ntsc);

      input->signal[tvy][tvx+0]= ntsc[(tvx+0)&3];
      input->signal[tvy][tvx+1]= ntsc[(tvx+1)&3];
      input->signal[tvy][tvx+2]= ntsc[(tvx+2)&3];
      input->signal[tvy][tvx+3]= ntsc[(tvx+3)&3];
    }
  }
}

PROTO_DLL int
analogtv_load_bitmap(const char *path, unsigned int **image_data, int *w, int *h) {
  FILE* bitmap;
  unsigned char bitmap_info[54];
  unsigned char *bitmap_data;
  int
    row,
    j,
    col = 0, /* Indexer for output data. */
    size, size_source_bytes, w_padded;
  
  assert(4 == sizeof(int));

  bitmap = fopen(path, "rb");
  if (NULL == bitmap) {
    return 1;
  }

  fread(bitmap_info, sizeof(unsigned char), 54, bitmap);
  *w = *(int*)&bitmap_info[18];
  *h = *(int*)&bitmap_info[22];

  /* Calculate bitmap row padding. */
  w_padded = ((*w) * 3 + 3) & (~3);

  size = (*w) * (*h);
  size_source_bytes = size*3; /* 3 Colors */

  bitmap_data = calloc(w_padded, sizeof(unsigned char));

  *image_data = calloc(size, sizeof(unsigned));

  for (row = 0; (*h)>row; row++) {
    fread(bitmap_data, sizeof(unsigned char), w_padded, bitmap);
    for (j = 0, col = 0; ((*w) * 3) > j; j += 3, col++) {

      unsigned pixel = 0;
      unsigned char *in_pixel_b = (unsigned*)(&bitmap_data[j]);
      unsigned char *in_pixel_g = (unsigned*)(&bitmap_data[j + 1]);
      unsigned char *in_pixel_r = (unsigned*)(&bitmap_data[j + 2]);

      pixel |= *in_pixel_r;
      pixel = pixel << 8;
      pixel |= *in_pixel_g;
      pixel = pixel << 8;
      pixel |= *in_pixel_b;
      pixel = pixel << 8;

      /* Start from the bottom row. */
      int out_index = (((*h) - (row + 1)) * (*w)) + col;

      (*image_data)[out_index] = pixel;

      out_index = out_index;
    }
  }

  fclose(bitmap);

  return 0;
}

PROTO_DLL void
analogtv_color(int idx, int ntsc[4])
{
	double clr_tbl[16][3] = {
		{ 0,   0,   0 },   /* Black */
		{ 255, 255, 255 }, /* White */
		{ 136,   0,   0 }, /* Red */
		{ 170, 255, 238 }, /* Pale Green */
		{ 204,  68, 204 }, /* Fuschia */
		{ 0, 204,  85 },   /* Bright Green */
		{ 0,   0, 170 },   /* Dark Blue */
		{ 238, 238, 119 }, /* Yellow */
		{ 221, 136,  85 }, /* Salmon */
		{ 102,  68,   0 }, /* Brown */
		{ 255, 119, 119 }, /* Hot Pink */
		{ 51,  51,  51 },  /* Very Dark Gray */
		{ 119, 119, 119 }, /* Dark Gray */
		{ 170, 255, 102 }, /* Yellow-Green */
		{ 0, 136, 255 },   /* Cyan */
		{ 187, 187, 187 }  /* Gray */
	};

  /*
	int i;
	int rawy, rawi, rawq;
	
  rawy = (int)((5 * clr_tbl[idx][0] + 11 * clr_tbl[idx][1] + 2 * clr_tbl[idx][2]) / 64);
	rawi = (int)((10 * clr_tbl[idx][0] - 4 * clr_tbl[idx][1] - 5 * clr_tbl[idx][2]) / 64);
	rawq = (int)((3 * clr_tbl[idx][0] - 8 * clr_tbl[idx][1] + 5 * clr_tbl[idx][2]) / 64);

	ntsc[0] = rawy + rawq;
	ntsc[1] = rawy - rawi;
	ntsc[2] = rawy - rawq;
	ntsc[3] = rawy + rawi;

	for (i = 0; i<4; i++) {
		if (ntsc[i]>ANALOGTV_WHITE_LEVEL) ntsc[i] = ANALOGTV_WHITE_LEVEL;
		if (ntsc[i]<ANALOGTV_BLACK_LEVEL) ntsc[i] = ANALOGTV_BLACK_LEVEL;
	}
  */

  analogtv_rgb_to_ntsc(clr_tbl[idx][0], clr_tbl[idx][1], clr_tbl[idx][2], ntsc);
}

PROTO_DLL void
analogtv_rgb_to_ntsc(byte rawr, byte rawg, byte rawb, int ntsc[4]){

  int i;
  int rawy, rawi, rawq;

  /* RGB conversion taken from analogtv draw xpm */
  rawy = (int)((5 * rawr + 11 * rawg + 2 * rawb) / 64);
  rawi = (int)((10 * rawr - 4 * rawg - 5 * rawb) / 64);
  rawq = (int)((3 * rawr - 8 * rawg + 5 * rawb) / 64);

  ntsc[0] = rawy + rawq;
  ntsc[1] = rawy - rawi;
  ntsc[2] = rawy - rawq;
  ntsc[3] = rawy + rawi;

  for (i = 0; i<4; i++) {
    if (ntsc[i]>ANALOGTV_WHITE_LEVEL) ntsc[i] = ANALOGTV_WHITE_LEVEL;
    if (ntsc[i]<ANALOGTV_BLACK_LEVEL) ntsc[i] = ANALOGTV_BLACK_LEVEL;
  }
}