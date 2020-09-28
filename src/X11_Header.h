#pragma once

static_assert(OS_LINUX);

#define Font X11Font

#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>

#undef Font

#undef None // Conflicts with None in my code

enum X11EventType: int
{
	X11_KeyPress		     = KeyPress,           
	X11_KeyRelease		     = KeyRelease,
	X11_ButtonPress		     = ButtonPress,
	X11_ButtonRelease		 = ButtonRelease,
	X11_MotionNotify		 = MotionNotify,
	X11_EnterNotify		     = EnterNotify,
	X11_LeaveNotify		     = LeaveNotify,
	X11_FocusIn			     = FocusIn,
	X11_FocusOut		    = FocusOut,
	X11_KeymapNotify		= KeymapNotify,
	X11_Expose			    = Expose,
	X11_GraphicsExpose		= GraphicsExpose,
	X11_NoExpose		    = NoExpose,
	X11_VisibilityNotify	= VisibilityNotify,
	X11_CreateNotify		= CreateNotify,
	X11_DestroyNotify		= DestroyNotify,
	X11_UnmapNotify		    = UnmapNotify,
	X11_MapNotify		    = MapNotify,
	X11_MapRequest		    = MapRequest,
	X11_ReparentNotify		= ReparentNotify,
	X11_ConfigureNotify		= ConfigureNotify,
	X11_ConfigureRequest	= ConfigureRequest,
	X11_GravityNotify		= GravityNotify,
	X11_ResizeRequest		= ResizeRequest,
	X11_CirculateNotify		= CirculateNotify,
	X11_CirculateRequest	= CirculateRequest,
	X11_PropertyNotify		= PropertyNotify,
	X11_SelectionClear		= SelectionClear,
	X11_SelectionRequest	= SelectionRequest,
	X11_SelectionNotify		= SelectionNotify,
	X11_ColormapNotify		= ColormapNotify,
	X11_ClientMessage		= ClientMessage,
	X11_MappingNotify		= MappingNotify,
	X11_GenericEvent		= GenericEvent,	
};

REFLECT(X11EventType)
	ENUM_VALUE(X11_KeyPress);		    
	ENUM_VALUE(X11_KeyRelease);		    
	ENUM_VALUE(X11_ButtonPress);		    
	ENUM_VALUE(X11_ButtonRelease);		
	ENUM_VALUE(X11_MotionNotify);		
	ENUM_VALUE(X11_EnterNotify);		    
	ENUM_VALUE(X11_LeaveNotify);		    
	ENUM_VALUE(X11_FocusIn);			    
	ENUM_VALUE(X11_FocusOut);		    
	ENUM_VALUE(X11_KeymapNotify);		
	ENUM_VALUE(X11_Expose);			    
	ENUM_VALUE(X11_GraphicsExpose);		
	ENUM_VALUE(X11_NoExpose);		    
	ENUM_VALUE(X11_VisibilityNotify);	
	ENUM_VALUE(X11_CreateNotify);		
	ENUM_VALUE(X11_DestroyNotify);		
	ENUM_VALUE(X11_UnmapNotify);		    
	ENUM_VALUE(X11_MapNotify);		    
	ENUM_VALUE(X11_MapRequest);		    
	ENUM_VALUE(X11_ReparentNotify);		
	ENUM_VALUE(X11_ConfigureNotify);		
	ENUM_VALUE(X11_ConfigureRequest);	
	ENUM_VALUE(X11_GravityNotify);		
	ENUM_VALUE(X11_ResizeRequest);		
	ENUM_VALUE(X11_CirculateNotify);		
	ENUM_VALUE(X11_CirculateRequest);	
	ENUM_VALUE(X11_PropertyNotify);		
	ENUM_VALUE(X11_SelectionClear);		
	ENUM_VALUE(X11_SelectionRequest);	
	ENUM_VALUE(X11_SelectionNotify);		
	ENUM_VALUE(X11_ColormapNotify);		
	ENUM_VALUE(X11_ClientMessage);		
	ENUM_VALUE(X11_MappingNotify);		
	ENUM_VALUE(X11_GenericEvent);		
REFLECT_END();


#define _NET_WM_STATE_ADD    1
#define _NET_WM_STATE_REMOVE 0

#define WM_NormalState 1L // window normal state
#define WM_IconicState 3L // window minimized

#define GET_X11_ATOM(atom) XInternAtom(x11.display, #atom , false)

struct X11
{
    Display* display;
    int      screen;
    Window   window;
    GC       gc;
};

inline X11 x11;


// Not 100% accurate sorry, we're living in the Linux world.
inline bool is_x11_window_likely_maximized = false;



// Stolen from https://github.com/godotengine/godot/blob/master/platform/linuxbsd/display_server_x11.cpp
inline bool is_x11_window_maximized()
{
	Atom property = XInternAtom(x11.display, "_NET_WM_STATE", False);
	Atom type;
	int format;
	unsigned long len;
	unsigned long remaining;
	unsigned char *data = nullptr;
	bool retval = false;

	int result = XGetWindowProperty(
			x11.display,
			x11.window,
			property,
			0,
			1024,
			False,
			XA_ATOM,
			&type,
			&format,
			&len,
			&remaining,
			&data);

	if (result == Success && data) {
		Atom *atoms = (Atom *)data;
		Atom wm_act_max_horz = GET_X11_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ);
		Atom wm_act_max_vert = GET_X11_ATOM(_NET_WM_STATE_MAXIMIZED_VERT);
		bool found_wm_act_max_horz = false;
		bool found_wm_act_max_vert = 	false;

		for (uint64_t i = 0; i < len; i++) {
			if (atoms[i] == wm_act_max_horz) {
				found_wm_act_max_horz = true;
			}
			if (atoms[i] == wm_act_max_vert) {
				found_wm_act_max_vert = true;
			}

			if (found_wm_act_max_horz || found_wm_act_max_vert) {
				retval = true;
				break;
			}
		}

		XFree(data);
	}

	return retval;
}


inline void minimize_x11_window()
{
	XEvent xev = {};
	Atom wm_change = GET_X11_ATOM(WM_CHANGE_STATE);

	xev.type = ClientMessage;
	xev.xclient.window = x11.window;
	xev.xclient.message_type = wm_change;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = WM_IconicState;

	XSendEvent(x11.display, DefaultRootWindow(x11.display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);

	Atom wm_state  = GET_X11_ATOM(_NET_WM_STATE);
	Atom wm_hidden = GET_X11_ATOM(_NET_WM_STATE_HIDDEN);

	xev = {};

	xev.type = ClientMessage;
	xev.xclient.window = x11.window;
	xev.xclient.message_type = wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = _NET_WM_STATE_ADD;
	xev.xclient.data.l[1] = wm_hidden;

	XSendEvent(x11.display, DefaultRootWindow(x11.display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
}

inline void x11_toggle_window_maximization()
{
	bool is_window_maximized = is_x11_window_maximized();

	is_x11_window_likely_maximized = !is_window_maximized;

	XEvent e = {};
	e.type = ClientMessage;

	e.xclient.window = x11.window;
	e.xclient.message_type = GET_X11_ATOM(_NET_WM_STATE);
	e.xclient.format = 32;

	e.xclient.data.l[0] = is_window_maximized ? _NET_WM_STATE_REMOVE : _NET_WM_STATE_ADD;
	e.xclient.data.l[1] = GET_X11_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ);
	e.xclient.data.l[2] = GET_X11_ATOM(_NET_WM_STATE_MAXIMIZED_VERT);
	e.xclient.data.l[3] = 0;
	e.xclient.data.l[4] = 0;

	XSendEvent(x11.display, DefaultRootWindow(x11.display), false,
			SubstructureNotifyMask|SubstructureRedirectMask, &e);

#if 0
	if (!is_window_maximized)
	{
		for (int attempt = 0; !is_x11_window_maximized() && attempt < 50; attempt++)
		{
			usleep(10000);
		}
	}
#endif
}

inline void x11_get_mouse_position(int* out_x, int* out_y, Window window)
{
	Window out_root;
    Window out_child;

    int out_root_x, out_root_y;
    int out_child_x, out_child_y;

    u32 out_mask;

    XQueryPointer(x11.display, window, &out_root,
            &out_child, &out_root_x, &out_root_y, &out_child_x, &out_child_y,
            &out_mask);

	*out_x = out_child_x;
    *out_y = out_child_y;
}

inline void x11_get_window_position(int* out_x, int* out_y)
{
	int window_x, window_y;
	uint window_width, window_height, window_border_width, window_depth;
	Window window_root;

	XGetGeometry(x11.display, x11.window, &window_root, &window_x, &window_y, &window_width, &window_height, &window_border_width, &window_depth);

	*out_x = window_x;
	*out_y = window_y;
}

inline void x11_get_window_size(int* out_width, int* out_height)
{
	int window_x, window_y;
	uint window_width, window_height, window_border_width, window_depth;
	Window window_root;

	XGetGeometry(x11.display, x11.window, &window_root, &window_x, &window_y, &window_width, &window_height, &window_border_width, &window_depth);

	*out_width  = window_width;
	*out_height = window_height;
}

