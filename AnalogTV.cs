using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace libAnalogTV.Interop {
    public class AnalogTV {

        /* We don't handle interlace here */
        public const int ANALOGTV_V = 262;
        public const int ANALOGTV_TOP = 30;
        public const int ANALOGTV_VISLINES = 200;
        public const int ANALOGTV_BOT = ANALOGTV_TOP + ANALOGTV_VISLINES;

        /* The number of intensity levels we deal with for gamma correction &c */
        public const int ANALOGTV_CV_MAX = 1024;

        /* This really defines our sampling rate, 4x the colorburst     frequency. Handily equal to the Apple II's dot clock.     You could also make a case for using 3x the colorburst freq,     but 4x isn't hard to deal with. */
        public const int ANALOGTV_H = 912;

        /* MAX_LINEHEIGHT corresponds to 2400 vertical pixels, beyond which     it interpolates extra black lines. */
        public const int ANALOGTV_MAX_LINEHEIGHT = 12;
        /* Each line is 63500 nS long. The sync pulse is 4700 nS long, etc.     Define sync, back porch, colorburst, picture, and front porch     positions */
        public const int ANALOGTV_SYNC_START = 0;
        public const int ANALOGTV_BP_START = 4700 * ANALOGTV_H / 63500;
        public const int ANALOGTV_CB_START = 5800 * ANALOGTV_H / 63500;
        /* signal[row][ANALOGTV_PIC_START] is the first displayed pixel */
        public const int ANALOGTV_PIC_START = 9400 * ANALOGTV_H / 63500;
        public const int ANALOGTV_PIC_LEN = 52600 * ANALOGTV_H / 63500;
        public const int ANALOGTV_FP_START = 62000 * ANALOGTV_H / 63500;
        public const int ANALOGTV_PIC_END = ANALOGTV_FP_START;

        /* TVs scan past the edges of the picture tube, so normally you only     want to use about the middle 3/4 of the nominal scan line.  */
        public const int ANALOGTV_VIS_START = ANALOGTV_PIC_START + (ANALOGTV_PIC_LEN * 1 / 8);
        public const int ANALOGTV_VIS_END = ANALOGTV_PIC_START + (ANALOGTV_PIC_LEN * 7 / 8);
        public const int ANALOGTV_VIS_LEN = ANALOGTV_VIS_END - ANALOGTV_VIS_START;

        public const int ANALOGTV_HASHNOISE_LEN = 6;

        public const int ANALOGTV_GHOSTFIR_LEN = 4;

        /* analogtv.signal is in IRE units, as defined below: */
        public const int ANALOGTV_WHITE_LEVEL = 100;
        public const int ANALOGTV_GRAY50_LEVEL = 55;
        public const int ANALOGTV_GRAY30_LEVEL = 35;
        public const int ANALOGTV_BLACK_LEVEL = 10;
        public const int ANALOGTV_BLANK_LEVEL = 0;
        public const int ANALOGTV_SYNC_LEVEL = -40;
        public const int ANALOGTV_CB_LEVEL = 20;

        public const int ANALOGTV_SIGNAL_LEN = ANALOGTV_V * ANALOGTV_H;

#if false

        [StructLayout( LayoutKind.Sequential )]
        public struct analogtv_reception {

            public IntPtr input;
            public double ofs;
            public double level;
            public double multipath;
            public double freqerr;

            [MarshalAsAttribute( UnmanagedType.ByValArray, SizeConst = ANALOGTV_GHOSTFIR_LEN )]
            public double[] ghostfir;
            [MarshalAsAttribute( UnmanagedType.ByValArray, SizeConst = ANALOGTV_GHOSTFIR_LEN )]
            public double[] ghostfir2;

            public double hfloss;
            public double hfloss2;
        }

        [StructLayout( LayoutKind.Sequential )]
        public struct level {
            int index;
            double value;
        }

        public delegate void ThreadRunDelegate( IntPtr self );
        public delegate void ThreadDestroyDelegate( IntPtr self );

        [StructLayout( LayoutKind.Sequential )]
        public struct timeval {
            public int tv_sec;
            public int tv_usec;
        }

        [StructLayout( LayoutKind.Sequential )]
        struct threadpool {
            uint count;

            /* Copied from threadpool_class. No need for thread_create here, though. */
            UIntPtr thread_size;
            ThreadRunDelegate thread_run;
            ThreadDestroyDelegate thread_destroy;

            IntPtr serial_threads;
        };

        [StructLayout( LayoutKind.Sequential )]
        public struct analogtv {

            IntPtr window;

            threadpool threads;

            int n_colors;

            int interlace;
            int interlace_counter;

            float agclevel;

            /* If you change these, call analogtv_set_demod */
            Single tint_control, color_control, brightness_control, contrast_control;
            Single height_control, width_control, squish_control;
            Single horiz_desync;
            Single squeezebottom;
            Single powerup;

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

            int use_shm, use_cmap, use_color;
            int bilevel_signal;

            int red_invprec, red_shift;
            int green_invprec, green_shift;
            int blue_invprec, blue_shift;
            uint red_mask, green_mask, blue_mask;

            int usewidth, useheight, xrepl, subwidth;

            IntPtr image;

            int screen_xo, screen_yo; /* centers image in window */

            int flutter_horiz_desync;
            int flutter_tint;

            timeval last_display_time;
            int need_clear;


            /* Add hash (in the radio sense, not the programming sense.) These
               are the small white streaks that appear in quasi-regular patterns
               all over the screen when someone is running the vacuum cleaner or
               the blender. We also set shrinkpulse for one period which
               squishes the image horizontally to simulate the temporary line
               voltate drop when someone turns on a big motor */
            double hashnoise_rpm;
            int hashnoise_counter;
            [MarshalAs( UnmanagedType.ByValArray, SizeConst = ANALOGTV_V )]
            int[] hashnoise_times;
            [MarshalAs( UnmanagedType.ByValArray, SizeConst = ANALOGTV_V )]
            int[] hashnoise_signal;
            int hashnoise_on;
            int hashnoise_enable;
            int shrinkpulse;

            [MarshalAs( UnmanagedType.ByValArray, SizeConst = ANALOGTV_V )]
            Single[] crtload;

            [MarshalAs( UnmanagedType.ByValArray, SizeConst = ANALOGTV_CV_MAX )]
            uint[] red_values;
            [MarshalAs( UnmanagedType.ByValArray, SizeConst = ANALOGTV_CV_MAX )]
            uint[] green_values;
            [MarshalAs( UnmanagedType.ByValArray, SizeConst = ANALOGTV_CV_MAX )]
            uint[] blue_values;

            [MarshalAs( UnmanagedType.ByValArray, SizeConst = 256 )]
            UInt32[] colors;
            int cmap_y_levels;
            int cmap_i_levels;
            int cmap_q_levels;

            Single tint_i, tint_q;

            int cur_hsync;
            [MarshalAs( UnmanagedType.ByValArray, SizeConst = ANALOGTV_V )]
            int[] line_hsync;
            int cur_vsync;
            [MarshalAs( UnmanagedType.ByValArray, SizeConst = 4 )]
            double[] cb_phase;
            [MarshalAs( UnmanagedType.ByValArray, SizeConst = ANALOGTV_V * 4 )]
            double[] line_cb_phase;

            int channel_change_cycles;
            double rx_signal_level;
            IntPtr rx_signal;


            [MarshalAs( UnmanagedType.ByValArray, SizeConst = (ANALOGTV_MAX_LINEHEIGHT + 1) * (ANALOGTV_MAX_LINEHEIGHT + 1) )]
            level[] leveltable;

            /* Only valid during draw. */
            uint random0, random1;
            double noiselevel;
            //const analogtv_reception*const * recs;
            IntPtr recs;
            uint rec_count;

            IntPtr signal_subtotals;

            float puheight;

            int test_five;
        }

#endif

#if false
        [StructLayout( LayoutKind.Sequential )]
        public struct analogtv {
            IntPtr window;

            IntPtr threads;

            int n_colors;

            int interlace;
            int interlace_counter;

            float agclevel;

            /* If you change these, call analogtv_set_demod */
            float tint_control, color_control, brightness_control, contrast_control;
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

            int use_shm, use_cmap, use_color;
            int bilevel_signal;

            int usewidth, useheight, xrepl, subwidth;
            IntPtr image;
            int screen_xo, screen_yo; /* centers image in window */

            int flutter_horiz_desync;
            int flutter_tint;

            IntPtr last_display_time;
            int need_clear;


            /* Add hash (in the radio sense, not the programming sense.) These
               are the small white streaks that appear in quasi-regular patterns
               all over the screen when someone is running the vacuum cleaner or
               the blender. We also set shrinkpulse for one period which
               squishes the image horizontally to simulate the temporary line
               voltate drop when someone turns on a big motor */
            double hashnoise_rpm;
            int hashnoise_counter;
            fixed int hashnoise_times[ANALOGTV_V];
            fixed int hashnoise_signal[ANALOGTV_V];
            int hashnoise_on;
            int hashnoise_enable;
            int shrinkpulse;

            fixed float crtload[ANALOGTV_V];

            fixed uint red_values[ANALOGTV_CV_MAX];
            fixed uint green_values[ANALOGTV_CV_MAX];
            fixed uint blue_values[ANALOGTV_CV_MAX];

            fixed ulong colors[256];
            int cmap_y_levels;
            int cmap_i_levels;
            int cmap_q_levels;

            float tint_i, tint_q;

            int cur_hsync;
            fixed int line_hsync[ANALOGTV_V];
            int cur_vsync;
            fixed double cb_phase[4];
            fixed double line_cb_phase[ANALOGTV_V * 4];

            int channel_change_cycles;
            double rx_signal_level;
            IntPtr rx_signal;

            //fixed level leveltable[(ANALOGTV_MAX_LINEHEIGHT + 1) * (ANALOGTV_MAX_LINEHEIGHT + 1)];

            [MarshalAsAttribute( UnmanagedType.ByValArray, SizeConst = ANALOGTV_MAX_LINEHEIGHT + 1 )]
            IntPtr[] leveltable;

            /* Only valid during draw. */
            uint random0, random1;
            double noiselevel;
            IntPtr recs;
            uint rec_count;

            IntPtr signal_subtotals;

            float puheight;
        }
#endif

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_reconfigure( IntPtr it );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_set_defaults( IntPtr it, StringBuilder prefix );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_release( IntPtr it );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern int analogtv_set_demod( IntPtr it );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_setup_frame( IntPtr it );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_setup_sync( IntPtr input, int do_cb, int do_ssavi );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern IntPtr analogtv_allocate( IntPtr window );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern IntPtr analogtv_input_allocate();

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern IntPtr analogtv_reception_allocate( float level, IntPtr input );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_draw( IntPtr it, double noiselevel, ref IntPtr recs, uint rec_count );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern int analogtv_load_ximage( IntPtr it, IntPtr input, IntPtr pic_im );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_reception_update( IntPtr inp );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_setup_teletext( IntPtr input );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_make_font( IntPtr window, IntPtr f, int w, int h, char[] fontname );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern int analogtv_font_pixel( IntPtr f, int c, int x, int y );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_font_set_pixel( IntPtr f, int c, int x, int y, int value, IntPtr it );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_font_set_char( IntPtr f, int c, char[] s, IntPtr it );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_lcp_to_ntsc( double luma, double chroma, double phase, int[] ntsc ); // ntsc[4]

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_color( int index, int[] ntsc ); // ntsc[4]

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_draw_solid( IntPtr input, int left, int right, int top, int bot, int[] ntsc ); // ntsc[4]

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_draw_solid_rel_lcp( IntPtr input, double left, double right, double top, double bot, double luma, double chroma, double phase );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_draw_char( IntPtr input, IntPtr f, int c, int x, int y, int[] ntsc ); // ntsc[4]

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_draw_string( IntPtr input, IntPtr f, char[] s, int x, int y, int[] ntsc ); // ntsc[4]

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_draw_string_centered( IntPtr input, IntPtr f, char[] s, int x, int y, int[] ntsc ); // ntsc[4]

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern void analogtv_draw_xpm( IntPtr tv, IntPtr input, char[] xpm, int left, int top );

        [DllImport( "libAnalogTV.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl )]
        public static extern int analogtv_handle_events( IntPtr it );
    }
}

