
#include <stdint.h>
#ifdef WIN32
#include <Windows.h>
#else
#include <X11/Xlib.h>
#endif
#include "analogtv.h"

analogtv* tv;
analogtv_reception* reception;

#ifdef WIN32
#define IDT_TIMER1 10001

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
		analogtv_reception_update(reception);
		analogtv_draw(tv, 0.04, &reception, 1);
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
		100, 100, 800, 600, NULL, NULL, hInst, NULL
	);

	ShowWindow( win, nShowCmd );

#else
int main(void) {
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

#endif

   reception = calloc( 1, sizeof( analogtv_reception ) );
   analogtv_input* inp = calloc( 1, sizeof( analogtv_input ) );
   int field_ntsc[4] = { 0 };
   uint8_t pixels[32][32];
   int x, y;
   for( x = 0 ; x < 32 ; x++ ) {
      for( y = 0 ; y < 32 ; y++ ) {
         pixels[x][y] = 0;
      }
   }

#ifdef WIN32
   tv = analogtv_allocate( win );
#else
   tv = analogtv_allocate( dpy, win );
#endif

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

#ifdef WIN32
   SetTimer(win, IDT_TIMER1, 1000, (TIMERPROC)NULL);

   MSG msg;

   ZeroMemory(&msg, sizeof(MSG));

   /* Message loop. */
   while (GetMessage(&msg, NULL, 0, 0)) {
	   TranslateMessage(&msg);
	   DispatchMessage(&msg);
   }
#else
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
#endif

   return 0;   
}
