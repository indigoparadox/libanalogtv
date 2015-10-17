
#include <stdint.h>
#include <time.h>
#ifdef WIN32
#include <Windows.h>
#elif defined X11
#include <X11/Xlib.h>
#endif
#include "analogtv.h"

analogtv* tv = NULL;
analogtv_reception* reception = NULL;

enum {
    SCREEN_WIDTH = 1060,
    SCREEN_HEIGHT = 768,
    PONG_WIDTH = 30,
    PONG_HEIGHT = 15,
    PONG_MAX_X = (ANALOGTV_VIS_LEN - PONG_WIDTH),
    PONG_MAX_Y = (ANALOGTV_VISLINES - PONG_HEIGHT),
    PONG_INC_DEFAULT_X = 0,
    PONG_INC_DEFAULT_Y = 0,
    PONG_COUNT = 1,
    UPDATE_USEC = 10,
};

static int pong_x[PONG_COUNT] = { 0 };
static int pong_y[PONG_COUNT] = { 0 };
static int pong_move_inc_x[PONG_COUNT];
static int pong_move_inc_y[PONG_COUNT];
static int pong_color[PONG_COUNT];
static unsigned int *image;
static int image_w;
static int image_h;

static void draw_pong(analogtv_input* inp) {
    int field_ntsc[4] = { 0 };
    int i;

    analogtv_lcp_to_ntsc(ANALOGTV_BLACK_LEVEL, 0.0, 0.0, field_ntsc);

    //analogtv_color(count++ % 10, field_ntsc);
    analogtv_color(ANALOGTV_COLOR_WHITE, field_ntsc);

    analogtv_draw_solid(inp,
        ANALOGTV_VIS_START, ANALOGTV_VIS_END,
        ANALOGTV_TOP, ANALOGTV_BOT,
        field_ntsc);

    for (i = 0; PONG_COUNT > i; i++) {
        analogtv_color(pong_color[i], field_ntsc);

        if (0 == i) {
          analogtv_draw_image(inp, image, ANALOGTV_VIS_START + pong_x[i],
            ANALOGTV_TOP + pong_y[i], image_w, image_h);
        } else {
          analogtv_draw_solid(inp,
            ANALOGTV_VIS_START + pong_x[i], ANALOGTV_VIS_START + pong_x[i] + PONG_WIDTH,
            ANALOGTV_TOP + pong_y[i], ANALOGTV_TOP + pong_y[i] + PONG_HEIGHT,
            field_ntsc);
        }
    }
}

static void update_pong() {
    int i;
    for (i = 0; PONG_COUNT > i; i++) {
        if (PONG_MAX_X <= pong_x[i] || 0 > pong_x[i]) {
            pong_move_inc_x[i] *= -1;
        }
        if (PONG_MAX_Y <= pong_y[i] || 0 > pong_y[i]) {
            pong_move_inc_y[i] *= -1;
        }
        pong_x[i] += pong_move_inc_x[i];
        pong_y[i] += pong_move_inc_y[i];
    }
}

#ifdef WIN32
#define IDT_TIMER_DRAW 10001
#define IDT_TIMER_PONG 10002

HWND win;

LRESULT CALLBACK _TVWinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	    case WM_DESTROY:
		    PostQuitMessage(0);
		    return 0;

	    case WM_CREATE:
		    break;

	    case WM_TIMER:
		    if (IDT_TIMER_DRAW == wParam) {
			    analogtv_reception_update(reception);
			    analogtv_draw(tv, 0.04, &reception, 1);
		    } else if (IDT_TIMER_PONG == wParam) {
          update_pong();
			    draw_pong(reception->input);
		    }
		    break;

        case WM_SIZE:
            if (NULL != tv) {
                analogtv_reconfigure(tv);
            }
            break;
    }

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

INT WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, INT nShowCmd) {

	WNDCLASSEX winclass;
	HBRUSH hBrush;
	ZeroMemory(&winclass, sizeof(WNDCLASSEX));

	hBrush = CreateSolidBrush(RGB(0, 0, 0));

	winclass.cbSize = sizeof(WNDCLASSEX);
	winclass.hbrBackground = hBrush;
	winclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	winclass.hInstance = hInst;
	winclass.lpfnWndProc = (WNDPROC)_TVWinProc;
	winclass.lpszClassName = "AnalogTVTestClass";
	winclass.style = CS_HREDRAW | CS_VREDRAW;

	int result = RegisterClassEx(&winclass);
	if (!result) {
		/* TODO: Display error. */
		GetLastError();
		MessageBox(NULL, "Error registering window class.", "Error", MB_ICONERROR | MB_OK);
		return 1;
	}

	win = CreateWindowEx(
		0, "AnalogTVTestClass", "Analog TV",
		WS_OVERLAPPED | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX | WS_CAPTION | WS_SYSMENU,
		100, 100, SCREEN_WIDTH, SCREEN_HEIGHT, NULL, NULL, hInst, NULL
	);

	ShowWindow( win, nShowCmd );

#else
int main(void) {
#ifdef X11
   Display* dpy;
   Window rootwin;
   Window win;
   Colormap cmap;
   XEvent e;
   int scr;
   GC gc;
   int x11_fd;
   fd_set in_fds;
   struct timeval timev;

   if( !(dpy = XOpenDisplay( NULL )) ) {
      //fprintf( stderr, "ERROR: could not open display\n" );
      exit( 1 );
   }

   scr = DefaultScreen( dpy );
   rootwin = RootWindow( dpy, scr );
   cmap = DefaultColormap( dpy, scr );

   win = XCreateSimpleWindow(
      dpy, rootwin, 1, 1, SCREEN_WIDTH, SCREEN_HEIGHT, 0,
      BlackPixel( dpy, scr ),
      BlackPixel( dpy, scr )
   );

   gc = XCreateGC( dpy, win, 0, NULL );
   XSetForeground( dpy, gc, WhitePixel( dpy, scr ) );

   XSelectInput( dpy, win, ExposureMask | ButtonPressMask | StructureNotifyMask);

   XMapWindow( dpy, win );
   XFlush( dpy );

   x11_fd = ConnectionNumber( dpy );
#elif defined ALLEGRO
   allegro_init();

   BITMAP* dpy = screen;
#endif
#endif

   if (analogtv_load_bitmap("test24.bmp", &image, &image_w, &image_h)) {
     return 1;
   }

   int i;
   srand( (unsigned)time( NULL ) );
   for (i = 0; PONG_COUNT > i; i++) {
       pong_x[i] = rand() % (ANALOGTV_VIS_LEN - PONG_WIDTH);
       pong_y[i] = rand() % (ANALOGTV_VISLINES - PONG_HEIGHT);
       pong_color[i] = 2 + (rand() % 12);
       pong_move_inc_x[i] = PONG_INC_DEFAULT_X;
       pong_move_inc_y[i] = PONG_INC_DEFAULT_Y;
   }

   //reception = calloc( 1, sizeof( analogtv_reception ) );
   //analogtv_input* inp = calloc( 1, sizeof( analogtv_input ) );
   analogtv_input* inp = analogtv_input_allocate();
   reception = analogtv_reception_allocate(2.0f, inp);

#ifdef WIN32
   tv = analogtv_allocate( win );
#elif defined X11
   tv = analogtv_allocate( dpy, win );
#elif defined ALLEGRO
   tv = analogtv_allocate();
#endif

   analogtv_setup_sync( inp, 1, 0 );

   draw_pong(inp);

#ifdef WIN32
   SetTimer(win, IDT_TIMER_DRAW, UPDATE_USEC, (TIMERPROC)NULL);
   SetTimer(win, IDT_TIMER_PONG, UPDATE_USEC, (TIMERPROC)NULL);

   MSG msg;

   ZeroMemory(&msg, sizeof(MSG));

   /* Message loop. */
   while (GetMessage(&msg, NULL, 0, 0)) {
	   TranslateMessage(&msg);
	   DispatchMessage(&msg);
   }
#else
   //int counter = 0;
   while( 1 ) {
      update_pong();
      draw_pong( reception->input );
      analogtv_reception_update( reception );
      analogtv_draw( tv, 0.04, &reception, 1 );

      FD_ZERO( &in_fds );
      FD_SET( x11_fd, &in_fds );

      timev.tv_usec = UPDATE_USEC;

      // Wait for X Event or a Timer
      if( select( x11_fd + 1, &in_fds, 0, 0, &timev ) ) {
#if defined DEBUG && defined VERBOSE
          printf("Event Received!\n");
#endif
      } else {
#if defined DEBUG && defined VERBOSE
          printf("Timer Fired!\n");
#endif
      }

      // Handle XEvents and flush the input 
      while( XPending( dpy ) ) {
          XNextEvent( dpy, &e );
      
          if( e.type == ConfigureNotify ) {
              analogtv_reconfigure( tv );
          } else if( e.type == ButtonPress ) {
              break;
          }
      }
   }

   XCloseDisplay( dpy );
#endif

   return 0;   
}
#ifdef ALLEGRO
END_OF_MAIN()
#endif

