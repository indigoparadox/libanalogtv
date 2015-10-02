
#ifndef XSSEMU_H
#define XSSEMU_H

#ifdef WIN32
//#define Display HDC
#define ZPixmap NULL
//#define Window WINDOW
//#define XImage BITMAP
#define LSBFirst 1
#define MSBFirst 0

typedef struct {
	unsigned long pixel;			/* pixel value */
	unsigned short red, green, blue;	/* rgb values */
	char flags;				/* DoRed, DoGreen, DoBlue */
	char pad;
} XColor;
#endif

#endif /* XSSEMU_H */