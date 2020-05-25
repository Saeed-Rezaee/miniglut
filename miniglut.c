/*
MiniGLUT - minimal GLUT subset without dependencies
Copyright (C) 2020  John Tsiombikas <nuclear@member.fsf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifdef MINIGLUT_USE_LIBC
#include <stdlib.h>
#endif

#if defined(__unix__)

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <GL/glx.h>
#define BUILD_X11

#ifndef GLX_SAMPLE_BUFFERS_ARB
#define GLX_SAMPLE_BUFFERS_ARB	100000
#define GLX_SAMPLES_ARB			100001
#endif
#ifndef GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB
#define GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB	0x20b2
#endif

static Display *dpy;
static Window win, root;
static int scr;
static GLXContext ctx;
static Atom xa_wm_proto, xa_wm_del_win;
static unsigned int evmask;

#elif defined(_WIN32)

#include <windows.h>
#define BUILD_WIN32

static HWND win;
static HDC dc;
static HGLRC ctx;

#else
#error unsupported platform
#endif

#include <GL/gl.h>
#include "miniglut.h"

struct ctx_info {
	int rsize, gsize, bsize, asize;
	int zsize, ssize;
	int dblbuf;
	int samples;
	int stereo;
	int srgb;
};

static void create_window(const char *title);
static void get_window_pos(Window win, int *x, int *y);
static void get_window_size(Window win, int *w, int *h);
static void get_screen_size(Window win, int *scrw, int *scrh);

static long get_msec(void);
static void panic(const char *msg);
static void sys_exit(int status);
static int sys_write(int fd, const void *buf, int count);


static int init_x, init_y, init_width = 256, init_height = 256;
static unsigned int init_mode;

static struct ctx_info ctx_info;
static int cur_cursor = GLUT_CURSOR_INHERIT;

static glut_cb cb_display;
static glut_cb cb_idle;
static glut_cb_reshape cb_reshape;
static glut_cb_state cb_vis, cb_entry;
static glut_cb_keyb cb_keydown, cb_keyup;
static glut_cb_special cb_skeydown, cb_skeyup;
static glut_cb_mouse cb_mouse;
static glut_cb_motion cb_motion, cb_passive;
static glut_cb_sbmotion cb_sball_motion, cb_sball_rotate;
static glut_cb_sbbutton cb_sball_button;

static int win_width, win_height;
static int mapped;
static int quit;
static int upd_pending, reshape_pending;
static int modstate;


void glutInit(int *argc, char **argv)
{
#ifdef BUILD_X11
	if(!(dpy = XOpenDisplay(0))) {
		panic("Failed to connect to the X server\n");
	}
	scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	xa_wm_proto = XInternAtom(dpy, "WM_PROTOCOLS", False);
	xa_wm_del_win = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

	evmask = ExposureMask | StructureNotifyMask;
#endif
}

void glutInitWindowPosition(int x, int y)
{
	init_x = x;
	init_y = y;
}

void glutInitWindowSize(int xsz, int ysz)
{
	init_width = xsz;
	init_height = ysz;
}

void glutInitDisplayMode(unsigned int mode)
{
	init_mode = mode;
}

void glutCreateWindow(const char *title)
{
	create_window(title);
}

void glutExit(void)
{
	quit = 1;
}

void glutMainLoop(void)
{
	while(!quit) {
		glutMainLoopEvent();
	}
}

void glutPostRedisplay(void)
{
	upd_pending = 1;
}

#define UPD_EVMASK(x) \
	do { \
		if(func) { \
			evmask |= x; \
		} else { \
			evmask &= ~(x); \
		} \
		if(win) XSelectInput(dpy, win, evmask); \
	} while(0)


void glutIdleFunc(glut_cb func)
{
	cb_idle = func;
}

void glutDisplayFunc(glut_cb func)
{
	cb_display = func;
}

void glutReshapeFunc(glut_cb_reshape func)
{
	cb_reshape = func;
}

void glutVisibilityFunc(glut_cb_state func)
{
	cb_vis = func;
#ifdef BUILD_X11
	UPD_EVMASK(VisibilityChangeMask);
#endif
}

void glutEntryFunc(glut_cb_state func)
{
	cb_entry = func;
#ifdef BUILD_X11
	UPD_EVMASK(EnterWindowMask | LeaveWindowMask);
#endif
}

void glutKeyboardFunc(glut_cb_keyb func)
{
	cb_keydown = func;
#ifdef BUILD_X11
	UPD_EVMASK(KeyPressMask);
#endif
}

void glutKeyboardUpFunc(glut_cb_keyb func)
{
	cb_keyup = func;
#ifdef BUILD_X11
	UPD_EVMASK(KeyReleaseMask);
#endif
}

void glutSpecialFunc(glut_cb_special func)
{
	cb_skeydown = func;
#ifdef BUILD_X11
	UPD_EVMASK(KeyPressMask);
#endif
}

void glutSpecialUpFunc(glut_cb_special func)
{
	cb_skeyup = func;
#ifdef BUILD_X11
	UPD_EVMASK(KeyReleaseMask);
#endif
}

void glutMouseFunc(glut_cb_mouse func)
{
	cb_mouse = func;
#ifdef BUILD_X11
	UPD_EVMASK(ButtonPressMask | ButtonReleaseMask);
#endif
}

void glutMotionFunc(glut_cb_motion func)
{
	cb_motion = func;
#ifdef BUILD_X11
	UPD_EVMASK(ButtonMotionMask);
#endif
}

void glutPassiveMotionFunc(glut_cb_motion func)
{
	cb_passive = func;
#ifdef BUILD_X11
	UPD_EVMASK(PointerMotionMask);
#endif
}

void glutSpaceballMotionFunc(glut_cb_sbmotion func)
{
	cb_sball_motion = func;
}

void glutSpaceballRotateFunc(glut_cb_sbmotion func)
{
	cb_sball_rotate = func;
}

void glutSpaceballBittonFunc(glut_cb_sbbutton func)
{
	cb_sball_button = func;
}

int glutGet(unsigned int s)
{
	int x, y;
	switch(s) {
	case GLUT_WINDOW_X:
		get_window_pos(win, &x, &y);
		return x;
	case GLUT_WINDOW_Y:
		get_window_pos(win, &x, &y);
		return y;
	case GLUT_WINDOW_WIDTH:
		get_window_size(win, &x, &y);
		return x;
	case GLUT_WINDOW_HEIGHT:
		get_window_size(win, &x, &y);
		return y;
	case GLUT_WINDOW_BUFFER_SIZE:
		return ctx_info.rsize + ctx_info.gsize + ctx_info.bsize + ctx_info.asize;
	case GLUT_WINDOW_STENCIL_SIZE:
		return ctx_info.ssize;
	case GLUT_WINDOW_DEPTH_SIZE:
		return ctx_info.zsize;
	case GLUT_WINDOW_RED_SIZE:
		return ctx_info.rsize;
	case GLUT_WINDOW_GREEN_SIZE:
		return ctx_info.gsize;
	case GLUT_WINDOW_BLUE_SIZE:
		return ctx_info.bsize;
	case GLUT_WINDOW_ALPHA_SIZE:
		return ctx_info.asize;
	case GLUT_WINDOW_DOUBLEBUFFER:
		return ctx_info.dblbuf;
	case GLUT_WINDOW_RGBA:
		return 1;
	case GLUT_WINDOW_NUM_SAMPLES:
		return ctx_info.samples;
	case GLUT_WINDOW_STEREO:
		return ctx_info.stereo;
	case GLUT_WINDOW_SRGB:
		return ctx_info.srgb;
	case GLUT_WINDOW_CURSOR:
		return cur_cursor;
	case GLUT_SCREEN_WIDTH:
		get_screen_size(win, &x, &y);
		return x;
	case GLUT_SCREEN_HEIGHT:
		get_screen_size(win, &x, &y);
		return y;
	case GLUT_INIT_DISPLAY_MODE:
		return init_mode;
	case GLUT_INIT_WINDOW_X:
		return init_x;
	case GLUT_INIT_WINDOW_Y:
		return init_y;
	case GLUT_INIT_WINDOW_WIDTH:
		return init_width;
	case GLUT_INIT_WINDOW_HEIGHT:
		return init_height;
	case GLUT_ELAPSED_TIME:
		return get_msec();
	default:
		break;
	}
	return 0;
}

int glutGetModifiers(void)
{
	return modstate;
}

static int is_space(int c)
{
	return c == ' ' || c == '\t' || c == '\v' || c == '\n' || c == '\r';
}

static const char *skip_space(const char *s)
{
	while(*s && is_space(*s)) s++;
	return s;
}

int glutExtensionSupported(char *ext)
{
	const char *str, *eptr;

	if(!(str = (const char*)glGetString(GL_EXTENSIONS))) {
		return 0;
	}

	while(*str) {
		str = skip_space(str);
		eptr = skip_space(ext);
		while(*str && !is_space(*str) && *eptr && *str == *eptr) {
			str++;
			eptr++;
		}
		if((!*str || is_space(*str)) && !*eptr) {
			return 1;
		}
		while(*str && !is_space(*str)) str++;
	}

	return 0;
}

/* TODO */
void glutSolidSphere(float rad)
{
}

void glutWireSphere(float rad)
{
}

void glutSolidCube(float sz)
{
}

void glutWireCube(float sz)
{
}

void glutSolidTorus(float inner_rad, float outer_rad, float sides, float rings)
{
}

void glutWireTorus(float inner_rad, float outer_rad, float sides, float rings)
{
}

void glutSolidTeapot(float size)
{
}

void glutWireTeapot(float size)
{
}


#ifdef BUILD_X11
static void handle_event(XEvent *ev);

void glutMainLoopEvent(void)
{
	XEvent ev;

	if(!cb_display) {
		panic("display callback not set");
	}

	for(;;) {
		if(!upd_pending && !cb_idle) {
			XNextEvent(dpy, &ev);
			handle_event(&ev);
			if(quit) return;
		}
		while(XPending(dpy)) {
			XNextEvent(dpy, &ev);
			handle_event(&ev);
			if(quit) return;
		}

		if(cb_idle) {
			cb_idle();
		}

		if(upd_pending && mapped) {
			upd_pending = 0;
			cb_display();
		}
	}
}

static KeySym translate_keysym(KeySym sym)
{
	switch(sym) {
	case XK_Escape:
		return 27;
	case XK_BackSpace:
		return '\b';
	case XK_Linefeed:
		return '\r';
	case XK_Return:
		return '\n';
	case XK_Delete:
		return 127;
	case XK_Tab:
		return '\t';
	default:
		break;
	}
	return sym;
}

static void handle_event(XEvent *ev)
{
	KeySym sym;

	switch(ev->type) {
	case MapNotify:
		mapped = 1;
		break;
	case UnmapNotify:
		mapped = 0;
		break;
	case ConfigureNotify:
		if(cb_reshape && (ev->xconfigure.width != win_width || ev->xconfigure.height != win_height)) {
			win_width = ev->xconfigure.width;
			win_height = ev->xconfigure.height;
			cb_reshape(ev->xconfigure.width, ev->xconfigure.height);
		}
		break;

	case ClientMessage:
		if(ev->xclient.message_type == xa_wm_proto) {
			if(ev->xclient.data.l[0] == xa_wm_del_win) {
				quit = 1;
			}
		}
		break;

	case Expose:
		upd_pending = 1;
		break;

	case KeyPress:
	case KeyRelease:
		modstate = ev->xkey.state & (ShiftMask | ControlMask | Mod1Mask);
		if(!(sym = XLookupKeysym(&ev->xkey, 0))) {
			break;
		}
		sym = translate_keysym(sym);
		if(sym < 256) {
			if(ev->type == KeyPress) {
				if(cb_keydown) cb_keydown((unsigned char)sym, ev->xkey.x, ev->xkey.y);
			} else {
				if(cb_keyup) cb_keyup((unsigned char)sym, ev->xkey.x, ev->xkey.y);
			}
		} else {
			if(ev->type == KeyPress) {
				if(cb_skeydown) cb_skeydown(sym, ev->xkey.x, ev->xkey.y);
			} else {
				if(cb_skeyup) cb_skeyup(sym, ev->xkey.x, ev->xkey.y);
			}
		}
		break;

	case ButtonPress:
	case ButtonRelease:
		modstate = ev->xbutton.state & (ShiftMask | ControlMask | Mod1Mask);
		if(cb_mouse) {
			int bn = ev->xbutton.button - Button1;
			cb_mouse(bn, ev->type == ButtonPress ? GLUT_DOWN : GLUT_UP,
					ev->xbutton.x, ev->xbutton.y);
		}
		break;

	case MotionNotify:
		if(ev->xmotion.state & (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)) {
			if(cb_motion) cb_motion(ev->xmotion.x, ev->xmotion.y);
		} else {
			if(cb_passive) cb_passive(ev->xmotion.x, ev->xmotion.y);
		}
		break;

	case VisibilityNotify:
		if(cb_vis) {
			cb_vis(ev->xvisibility.state == VisibilityFullyObscured ? GLUT_NOT_VISIBLE : GLUT_VISIBLE);
		}
		break;
	case EnterNotify:
		if(cb_entry) cb_entry(GLUT_ENTERED);
		break;
	case LeaveNotify:
		if(cb_entry) cb_entry(GLUT_LEFT);
		break;
	}
}

void glutSwapBuffers(void)
{
	glXSwapBuffers(dpy, win);
}

void glutPositionWindow(int x, int y)
{
	XMoveWindow(dpy, win, x, y);
}

void glutReshapeWindow(int xsz, int ysz)
{
	XResizeWindow(dpy, win, xsz, ysz);
}

void glutFullScreen(void)
{
	/* TODO */
}

void glutSetWindowTitle(const char *title)
{
	XTextProperty tprop;
	if(!XStringListToTextProperty((char**)&title, 1, &tprop)) {
		return;
	}
	XSetWMName(dpy, win, &tprop);
	XFree(tprop.value);
}

void glutSetIconTitle(const char *title)
{
	XTextProperty tprop;
	if(!XStringListToTextProperty((char**)&title, 1, &tprop)) {
		return;
	}
	XSetWMIconName(dpy, win, &tprop);
	XFree(tprop.value);
}

void glutSetCursor(int cidx)
{
	Cursor cur = None;

	switch(cidx) {
	case GLUT_CURSOR_LEFT_ARROW:
		cur = XCreateFontCursor(dpy, XC_left_ptr);
		break;
	case GLUT_CURSOR_INHERIT:
		break;
	case GLUT_CURSOR_NONE:
		/* TODO */
	default:
		return;
	}

	XDefineCursor(dpy, win, cur);
	cur_cursor = cidx;
}

static XVisualInfo *choose_visual(unsigned int mode)
{
	XVisualInfo *vi;
	int attr[32] = {
		GLX_RGBA,
		GLX_DOUBLEBUFFER,
		GLX_RED_SIZE, 4,
		GLX_GREEN_SIZE, 4,
		GLX_BLUE_SIZE, 4
	};
	int *aptr = attr + 8;
	int *samples = 0;

	if(mode & GLUT_ALPHA) {
		*aptr++ = GLX_ALPHA_SIZE;
		*aptr++ = 4;
	}
	if(mode & GLUT_DEPTH) {
		*aptr++ = GLX_DEPTH_SIZE;
		*aptr++ = 16;
	}
	if(mode & GLUT_STENCIL) {
		*aptr++ = GLX_STENCIL_SIZE;
		*aptr++ = 1;
	}
	if(mode & GLUT_ACCUM) {
		*aptr++ = GLX_ACCUM_RED_SIZE; *aptr++ = 1;
		*aptr++ = GLX_ACCUM_GREEN_SIZE; *aptr++ = 1;
		*aptr++ = GLX_ACCUM_BLUE_SIZE; *aptr++ = 1;
	}
	if(mode & GLUT_STEREO) {
		*aptr++ = GLX_STEREO;
	}
	if(mode & GLUT_SRGB) {
		*aptr++ = GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB;
	}
	if(mode & GLUT_MULTISAMPLE) {
		*aptr++ = GLX_SAMPLE_BUFFERS_ARB;
		*aptr++ = 1;
		*aptr++ = GLX_SAMPLES_ARB;
		samples = aptr;
		*aptr++ = 32;
	}
	*aptr++ = None;

	if(!samples) {
		return glXChooseVisual(dpy, scr, attr);
	}
	while(!(vi = glXChooseVisual(dpy, scr, attr)) && *samples) {
		*samples >>= 1;
		if(!*samples) {
			aptr[-3] = None;
		}
	}
	return vi;
}

static void create_window(const char *title)
{
	XSetWindowAttributes xattr;
	XVisualInfo *vi;
	unsigned int xattr_mask;
	unsigned int mode = init_mode;

	if(!(vi = choose_visual(mode))) {
		mode &= ~GLUT_SRGB;
		if(!(vi = choose_visual(mode))) {
			panic("Failed to find compatible visual\n");
		}
	}

	if(!(ctx = glXCreateContext(dpy, vi, 0, True))) {
		XFree(vi);
		panic("Failed to create OpenGL context\n");
	}

	glXGetConfig(dpy, vi, GLX_RED_SIZE, &ctx_info.rsize);
	glXGetConfig(dpy, vi, GLX_GREEN_SIZE, &ctx_info.gsize);
	glXGetConfig(dpy, vi, GLX_BLUE_SIZE, &ctx_info.bsize);
	glXGetConfig(dpy, vi, GLX_ALPHA_SIZE, &ctx_info.asize);
	glXGetConfig(dpy, vi, GLX_DEPTH_SIZE, &ctx_info.zsize);
	glXGetConfig(dpy, vi, GLX_STENCIL_SIZE, &ctx_info.ssize);
	glXGetConfig(dpy, vi, GLX_DOUBLEBUFFER, &ctx_info.dblbuf);
	glXGetConfig(dpy, vi, GLX_STEREO, &ctx_info.stereo);
	glXGetConfig(dpy, vi, GLX_SAMPLES_ARB, &ctx_info.samples);
	glXGetConfig(dpy, vi, GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB, &ctx_info.srgb);

	xattr.background_pixel = BlackPixel(dpy, scr);
	xattr.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
	xattr_mask = CWBackPixel | CWColormap;
	if(!(win = XCreateWindow(dpy, root, init_x, init_y, init_width, init_height, 0,
			vi->depth, InputOutput, vi->visual, xattr_mask, &xattr))) {
		XFree(vi);
		glXDestroyContext(dpy, ctx);
		panic("Failed to create window\n");
	}
	XFree(vi);

	XSelectInput(dpy, win, evmask);

	glutSetWindowTitle(title);
	glutSetIconTitle(title);
	XSetWMProtocols(dpy, win, &xa_wm_del_win, 1);
	XMapWindow(dpy, win);

	glXMakeCurrent(dpy, win, ctx);
}

static void get_window_pos(Window win, int *x, int *y)
{
	XWindowAttributes wattr;
	XGetWindowAttributes(dpy, win, &wattr);
	*x = wattr.x;
	*y = wattr.y;
}

static void get_window_size(Window win, int *w, int *h)
{
	XWindowAttributes wattr;
	XGetWindowAttributes(dpy, win, &wattr);
	*w = wattr.width;
	*h = wattr.height;
}

static void get_screen_size(Window win, int *scrw, int *scrh)
{
	XWindowAttributes wattr;
	XGetWindowAttributes(dpy, root, &wattr);
	*scrw = wattr.width;
	*scrh = wattr.height;
}
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <sys/time.h>

static long get_msec(void)
{
	static struct timeval tv0;
	struct timeval tv;

	gettimeofday(&tv, 0);
	if(tv0.tv_sec == 0 && tv0.tv_usec == 0) {
		tv0 = tv;
		return 0;
	}
	return (tv.tv_sec - tv0.tv_sec) * 1000 + (tv.tv_usec - tv0.tv_usec) / 1000;
}
#endif
#ifdef _WIN32
static long get_msec(void)
{
	static long t0;

	if(!t0) {
		t0 = timeGetTime();
		return 0;
	}
	return timeGetTime() - t0;
}
#endif

static void panic(const char *msg)
{
	const char *end = msg;
	while(*end) end++;
	sys_write(2, msg, end - msg);
	sys_exit(1);
}


#ifdef MINIGLUT_USE_LIBC
static void sys_exit(int status)
{
	exit(status);
}

static int sys_write(int fd, const void *buf, int count)
{
	return write(fd, buf, count);
}
#else	/* !MINIGLUT_USE_LIBC */

#ifdef __linux__

#ifdef __x86_64__
static void sys_exit(int status)
{
	asm volatile(
		"syscall\n\t"
		:: "a"(60), "D"(status)
	);
}
static int sys_write(int fd, const void *buf, int count)
{
	long res;
	asm volatile(
		"syscall\n\t"
		: "=a"(res)
		: "a"(1), "D"(fd), "S"(buf), "d"(count)
	);
	return res;
}
#endif
#ifdef __i386__
static void sys_exit(int status)
{
	asm volatile(
		"mov $1, %%eax\n\t"
		"int $0x80\n\t"
		:: "b"(status)
		: "eax"
	);
}
static int sys_write(int fd, const void *buf, int count)
{
	int res;
	asm volatile(
		"mov $4, %%eax\n\t"
		"int $0x80\n\t"
		:: "b"(fd), "c"(buf), "d"(count));
	return res;
}
#endif

#endif	/* __linux__ */

#ifdef _WIN32
static int sys_exit(int status)
{
	ExitProcess(status);
}
static int sys_write(int fd, const void *buf, int count)
{
	int wrsz;

	HANDLE out = GetStdHandle(fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
	if(!WriteFile(out, buf, count, &wrsz, 0)) {
		return -1;
	}
	return wrsz;
}
#endif	/* _WIN32 */

#endif	/* !MINIGLUT_USE_LIBC */
