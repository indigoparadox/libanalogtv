
#include <stdint.h>
#include <X11/Xlib.h>
#include "analogtv.h"

#ifdef WIN32

#else

int main( void ) {
   Display* dpy;
   Window rootwin;
   Window win;
   Colormap cmap;
   XEvent e;
   int scr;
   GC gc;

   if( !(dpy = XOpenDisplay( NULL )) ) {
      //fprintf( stderr, "ERROR: could not open display\n" );
      exit( 1 );
   }

   scr = DefaultScreen( dpy );
   rootwin = RootWindow( dpy, scr );
   cmap = DefaultColormap( dpy, scr );

   win = XCreateSimpleWindow(
      dpy, rootwin, 1, 1, 1024, 768, 0,
      BlackPixel( dpy, scr ),
      BlackPixel( dpy, scr )
   );

   gc = XCreateGC( dpy, win, 0, NULL );
   XSetForeground( dpy, gc, WhitePixel( dpy, scr ) );

   XSelectInput( dpy, win, ExposureMask | ButtonPressMask );

   XMapWindow( dpy, win );

   analogtv_reception* reception = calloc( 1, sizeof( analogtv_reception ) );
   analogtv_input* inp = calloc( 1, sizeof( analogtv_input ) );
   int field_ntsc[4] = { 0 };
   uint8_t pixels[32][32];
   int x, y;
   for( x = 0 ; x < 32 ; x++ ) {
      for( y = 0 ; y < 32 ; y++ ) {
         pixels[x][y] = 0;
      }
   }

   analogtv* tv = analogtv_allocate( dpy, win );

   analogtv_set_defaults( tv, "" );
   analogtv_setup_sync( inp, 1, 0 );

   reception->input = inp;
   reception->level = 2.0;
   reception->ofs = 0;
   reception->multipath = 0.0;

   analogtv_lcp_to_ntsc( ANALOGTV_BLACK_LEVEL, 0.0, 0.0, field_ntsc );

   analogtv_draw_solid( inp,
                        ANALOGTV_VIS_START, ANALOGTV_VIS_END,
                        ANALOGTV_TOP, ANALOGTV_BOT,
                        field_ntsc );

   while( 1 ) {
      analogtv_reception_update( reception );
      analogtv_draw( tv, 0.04, &reception, 1 );
      /*
      XNextEvent( dpy, &e );
      if( e.type == Expose && e.xexpose.count < 1 ) {
         //XDrawString( dpy, win, gc, 10, 10, "Hello World!", 12 );
      } else if( e.type == ButtonPress ) {
         break;
      }
      */
   }

   XCloseDisplay( dpy );

   return 0;   
}

#endif

