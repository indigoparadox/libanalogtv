
#include <stdint.h>
#ifdef WIN32
#include <Windows.h>
#elif defined X11
#include <X11/Xlib.h>
#endif
#include "analogtv.h"
#include "yarandom.h"

analogtv* tv;
analogtv_reception* reception;

#define SCREEN_WIDTH 472
#define SCREEN_HEIGHT 400

#define PONG_WIDTH 30
#define PONG_HEIGHT 30
#define PONG_MAX_X 240
#define PONG_MAX_Y 160
#define PONG_INC_DEFAULT_X 5
#define PONG_INC_DEFAULT_Y 5

static int pong_x = 0;
static int pong_y = 0;
static int pong_move_inc_x = PONG_INC_DEFAULT_X;
static int pong_move_inc_y = PONG_INC_DEFAULT_Y;

static void draw_pong(analogtv_input* inp, int x, int y) {
    int field_ntsc[4] = { 0 };

    analogtv_lcp_to_ntsc(ANALOGTV_BLACK_LEVEL, 0.0, 0.0, field_ntsc);

    analogtv_draw_solid(inp,
        ANALOGTV_VIS_START, ANALOGTV_VIS_END,
        ANALOGTV_TOP, ANALOGTV_BOT,
        field_ntsc);

    analogtv_color(1, field_ntsc);

    analogtv_draw_solid(inp,
        ANALOGTV_VIS_START + x, ANALOGTV_VIS_START + x + PONG_WIDTH,
        ANALOGTV_TOP + y, ANALOGTV_TOP + y + PONG_HEIGHT,
        field_ntsc);
}

static void update_pong() {
    if (PONG_MAX_X <= pong_x || 0 > pong_x) {
        pong_move_inc_x *= -1;
    }
    if (PONG_MAX_Y <= pong_y || 0 > pong_y) {
        pong_move_inc_y *= -1;
    }
    pong_x += pong_move_inc_x;
    pong_y += pong_move_inc_y;
}

#ifdef WIN32
#define IDT_TIMER_DRAW 10001
#define IDT_TIMER_PONG 10002

HWND win;

LRESULT CALLBACK _TVWinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HDC hdc;
	PAINTSTRUCT sPaint;
	HGDIOBJ hfDefault;
	HWND hWndButton;

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
			draw_pong(reception->input, pong_x, pong_y);
		}
		break;

		/*
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &sPaint);
		_IPWinBGPaint(hWnd, hdc);
		EndPaint(hWnd, &sPaint);
		goto cleanup;
		*/
}

cleanup:

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
		//nRetVal = 1;
		//goto cleanup;
		return 1;
	}

	win = CreateWindowEx(
		NULL, "AnalogTVTestClass", "Analog TV",
		WS_OVERLAPPED | WS_MINIMIZEBOX | WS_CAPTION | WS_SYSMENU,
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

   XSelectInput( dpy, win, ExposureMask | ButtonPressMask );

   XMapWindow( dpy, win );
#elif defined ALLEGRO
   allegro_init();

   BITMAP* dpy = screen;
#endif
#endif

   //reception = calloc( 1, sizeof( analogtv_reception ) );
   //analogtv_input* inp = calloc( 1, sizeof( analogtv_input ) );
   analogtv_input* inp = analogtv_input_allocate();
   reception = analogtv_reception_allocate(2.0f, inp);
   //uint8_t pixels[32][32];
   /*
   int x, y;
   for( x = 0 ; x < 32 ; x++ ) {
      for( y = 0 ; y < 32 ; y++ ) {
         pixels[x][y] = 0;
      }
   }
   */

#ifdef WIN32
   tv = analogtv_allocate( win );
#elif defined X11
   tv = analogtv_allocate( dpy, win );
#elif defined ALLEGRO
   tv = analogtv_allocate();
#endif

   //ya_rand_init(0);

   //analogtv_set_defaults( tv, "" );
   analogtv_setup_sync( inp, 1, 0 );

   /*
   reception->input = inp;
   reception->level = 2.0;
   reception->ofs = 0;
   reception->multipath = 0.0;
   */

   draw_pong(inp, 10, 10);

#ifdef WIN32
   SetTimer(win, IDT_TIMER_DRAW, 50, (TIMERPROC)NULL);
   SetTimer(win, IDT_TIMER_PONG, 100, (TIMERPROC)NULL);

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
      //if(0 == )
      update_pong();
      draw_pong(reception->input, pong_x, pong_y);
      analogtv_reception_update( reception );
      analogtv_draw( tv, 0.04, &reception, 1 );
      //counter++;
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
#endif

   return 0;   
}
#ifdef ALLEGRO
END_OF_MAIN()
#endif

