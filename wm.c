// wm.c - Simple Xorg window manager

// TODO
//  - Find a method to center upon new window spawning
//  - Snap windows with hjkl
//  - Functionize pointer_event()


/*
ButtonPress       A button has been pressed
ButtonRelease     A button has been released

KeyPress          A key has been pressed
KeyRelease        A key has been realease

EnterNotify       The pointer has entered a window
LeaveNotify       The pointer has left a window

MotionNotify      A pointer's motion occurrs within a window
ConfigureRequest  Request to change a window's attributes
MapRequest        Show window on screen
DestroyNotify     Kill the window

Mask        | Value | Key
------------+-------+------------
ShiftMask   |     1 | Shift
LockMask    |     2 | Caps Lock
ControlMask |     4 | Ctrl
Mod1Mask    |     8 | Alt
Mod2Mask    |    16 | Num Lock
Mod3Mask    |    32 | Scroll Lock
Mod4Mask    |    64 | Windows
Mod5Mask    |   128 | ???

*/


#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>


#define DEBUG


#define MAX( x, y )  (            \
	( x ) > ( y ) ? ( x ) : ( y ) \
) 


#define ABS( x ) (                 \
	( x ) > 0 ? ( x ) : -1 * ( x ) \
)

#define MIN( x, y )  (            \
	( x ) < ( y ) ? ( x ) : ( y ) \
) 

#define BETWEEN( x, a, z )  (   \
	( ( a ) > ( x ) ) ?         \
	( a ) : ( ( z ) < ( x ) ) ? \
    ( z ) : ( x )               \
)


#define LENGTH( x )  (                 \
	sizeof( ( x ) ) / sizeof( ( *x ) ) \
)


#define window_size( window, x, y, w, h )  ( \
	XGetGeometry(                                  \
		display,                                   \
		window,                                    \
		&(Window){0},                              \
		x,                                         \
		y,                                         \
		w,                                         \
		h,                                         \
		&(unsigned int){0},                        \
		&(unsigned int){0}                         \
	)                                              \
)                                                  \


#define SNAP
#ifdef SNAP 
	#define SNAP_PIXELS 20 
	//#define SNAP_RATIO ( ( double ) 1 / ( double ) 3 )

	#define GAPS
	#ifdef GAPS
		#define GAP_PIXELS 10
	#endif
#endif

#define BORDER 1

#define MINIMUM_SIZE 50


// Mask        | Value | Key
// ------------+-------+------------
// ShiftMask   |     1 | Shift
// LockMask    |     2 | Caps Lock
// ControlMask |     4 | Ctrl
// Mod1Mask    |     8 | Alt
// Mod2Mask    |    16 | Num Lock
// Mod3Mask    |    32 | Scroll Lock
// Mod4Mask    |    64 | Windows
// Mod5Mask    |   128 | ISO_Level3_Shift

 
#define MOD      Mod4Mask
#define CLEAN_MASK(mask) (                                       \
	mask &                                                       \
	~( LockMask | Mod2Mask | Mod3Mask | NumLockMask ) &          \
	( ShiftMask | ControlMask | Mod1Mask | Mod4Mask | Mod5Mask ) \
)


///////////////////////////////////////////////////////////////////////
// TYPES
///////////////////////////////////////////////////////////////////////


typedef union
{
	uint64_t x;
	void const *p;
} argument_t;


typedef struct 
{
    unsigned int mod;
   	KeySym key;
    void ( *f )( argument_t const a );
    argument_t a;
} key_input_t;


typedef struct client_t
{
	struct client_t *next;
	Window window;
} client_t;


///////////////////////////////////////////////////////////////////////
// FUNCTION DECLARATIONS
///////////////////////////////////////////////////////////////////////


void handle_event( XEvent * );
void pointer_event( XEvent * );
void configure_request( XEvent * );
void destroy_notify( XEvent * );
void enter_notify( XEvent * );
void key_event( XEvent * );
void map_request( XEvent * );
void window_add( Window );
void window_delete( Window );
void window_kill( argument_t const );
void window_current( Window );
void window_center( Window );
void window_fullscreen( argument_t const );
void window_next( argument_t const );
void window_previous( argument_t const );
void window_snap( int xf, int yf, int *x, int *y, int *w, int *h );
void window_push( argument_t const a );
void window_to_workspace( argument_t const );
void to_workspace( argument_t const );
void run( argument_t const );
void quit( argument_t const );
void grab_input();
static int xerror();


///////////////////////////////////////////////////////////////////////
// LITERALS
///////////////////////////////////////////////////////////////////////


static uint8_t  loop;
static Display  *display;
static Window   root;
static client_t *workspaces[9] = {0};
static uint8_t  workspace = 0;
static int32_t sw, sh;
static uint32_t NumLockMask = 0; 

static char const *terminal[] = {
//    "st", NULL 
};

static char const *menu[] = { 
//    "dmenu_run", NULL 
};

key_input_t const KEYS[] = {
//	{ MOD,           KEY,       f(),                 {0} },

	{ MOD,           XK_Return, run,                 { .p = terminal } },
	{ MOD,           XK_d,      run,                 { .p = menu } },
	{ MOD|ShiftMask, XK_q,      quit,                { 0 } },

	{ MOD,           XK_Tab,    window_next,         { 0 } },
	{ MOD|ShiftMask, XK_Tab,    window_previous,     { 0 } },
	{ MOD,           XK_f,      window_fullscreen,   { 0 } },
	{ MOD,           XK_q,      window_kill,         { 0 } },

	{ MOD,           XK_h,      window_push,         { .x = 2 } },
	{ MOD,           XK_j,      window_push,         { .x = 3 } },
	{ MOD,           XK_k,      window_push,         { .x = 1 } },
	{ MOD,           XK_l,      window_push,         { .x = 0 } },

	{ MOD,           XK_1,      to_workspace,        { .x = 0 } },
	{ MOD,           XK_2,      to_workspace,        { .x = 1 } },
	{ MOD,           XK_3,      to_workspace,        { .x = 2 } },
	{ MOD,           XK_4,      to_workspace,        { .x = 3 } },
	{ MOD,           XK_5,      to_workspace,        { .x = 4 } },
	{ MOD,           XK_6,      to_workspace,        { .x = 5 } },
	{ MOD,           XK_7,      to_workspace,        { .x = 6 } },
	{ MOD,           XK_8,      to_workspace,        { .x = 7 } },
	{ MOD,           XK_9,      to_workspace,        { .x = 8 } },
//	{ MOD,           XK_0,      to_workspace,        { .x = 0 } },

	{ MOD|ShiftMask, XK_1,      window_to_workspace, { .x = 0 } },
	{ MOD|ShiftMask, XK_2,      window_to_workspace, { .x = 1 } },
	{ MOD|ShiftMask, XK_3,      window_to_workspace, { .x = 2 } },
	{ MOD|ShiftMask, XK_4,      window_to_workspace, { .x = 3 } },
	{ MOD|ShiftMask, XK_5,      window_to_workspace, { .x = 4 } },
	{ MOD|ShiftMask, XK_6,      window_to_workspace, { .x = 5 } },
	{ MOD|ShiftMask, XK_7,      window_to_workspace, { .x = 6 } },
	{ MOD|ShiftMask, XK_8,      window_to_workspace, { .x = 7 } },
	{ MOD|ShiftMask, XK_9,      window_to_workspace, { .x = 8 } },
    
//	{ 0, XF86XK_AudioLowerVolume,  run,              { .p = voldown } },
//	{ 0, XF86XK_AudioRaiseVolume,  run,              { .p = volup } },
//	{ 0, XF86XK_AudioMute,         run,              { .p = volmute } },
//	{ 0, XF86XK_MonBrightnessUp,   run,              { .p = briup } },
//	{ 0, XF86XK_MonBrightnessDown, run,              { .p = bridown } },
};


////////////////////////////////////////////////////////////////////////////////
// EVENT
////////////////////////////////////////////////////////////////////////////////


// handle_event()
//
// Handle a the given XEvent
//
// e - The current XEvent

void handle_event( XEvent *e )
{
	switch( e->type )
	{
		case ButtonPress:
		case ButtonRelease:
	 	case MotionNotify:
			pointer_event( e );
			break;

	 	case KeyPress:         
		case KeyRelease:
			key_event( e );
			break;

	 	case MapRequest:
			map_request( e );
			break;

	 	case DestroyNotify:
			destroy_notify( e );
			break;

	 	case EnterNotify:
			enter_notify( e );
			break;

	 	case ConfigureRequest:
			configure_request( e );
			break;
	}
}


// pointer_event()
//
// Respond to mouse events
//
// e - The given XEvent

void pointer_event( XEvent *e )
{
	static XButtonEvent mouse;	
	static int32_t  x, y, w, h;

	if( e->type == MotionNotify && mouse.subwindow )
	{
		#ifdef DEBUG
			fputs( "MOTION NOTIFY\n", stderr );
		#endif

    	while( XCheckTypedEvent( display, MotionNotify, e ) );

	#ifdef SNAP

		int32_t nx, ny, nw, nh;
		nx = e->xbutton.x_root;
		ny = e->xbutton.y_root;
	
		if( mouse.button == 1 )
		{	
			if( SNAP_PIXELS < nx && nx < sw - SNAP_PIXELS &&
			    SNAP_PIXELS < ny && ny < sh - SNAP_PIXELS )
			{
		        nx = BETWEEN( x + nx - mouse.x_root, 0, sw - w );
		        ny = BETWEEN( y + ny - mouse.y_root, 0, sh - h );
		        nw = w;
				nh = h;
			}
			else
			{
				int32_t xf = 0, yf = 0;

				if ( nx <= SNAP_PIXELS )      xf = -1;
				if ( nx >= sw - SNAP_PIXELS ) xf = 1;
				if ( ny <= SNAP_PIXELS )      yf = -1;
				if ( ny >= sh - SNAP_PIXELS ) yf = 1;

				window_snap( xf, yf, &nx, &ny, &nw, &nh );
			}		
		}
		// Resize
		else if( mouse.button == 3 )
		{
		    nw = BETWEEN( w + nx - mouse.x_root, 0, sw - x );
		    nh = BETWEEN( h + ny - mouse.y_root, 0, sh - y );
			nx = x;
			ny = y;
		}
		
		XMoveResizeWindow( 
			display, 
			mouse.subwindow,
			nx,
			ny,
			nw,
			nh 
		);

	#else // SNAP

    	int32_t dx = e->xbutton.x_root - mouse.x_root;
    	int32_t dy = e->xbutton.y_root - mouse.y_root;

    	XMoveResizeWindow(
			display, 
			mouse.subwindow,
    	    BETWEEN( x + ( mouse.button == 1 ? dx : 0 ), 0, sw - w ),
    	    BETWEEN( y + ( mouse.button == 1 ? dy : 0 ), 0, sh - h ),
    	    BETWEEN( w + ( mouse.button == 3 ? dx : 0), MINIMUM_SIZE, sw - x ),
    	    BETWEEN( h + ( mouse.button == 3 ? dy : 0), MINIMUM_SIZE, sh - y )
		);

	#endif // SNAP
	}
	else if( e->type == ButtonPress )
	{	
		#ifdef DEBUG
			fputs( "BUTTON PRESS\n", stderr );
	    #endif

		if (!e->xbutton.subwindow) return;
	
	    mouse = e->xbutton;
		window_size( mouse.subwindow, &x, &y, &w, &h );
		window_current( mouse.subwindow );
	}
	else if( e->type == ButtonRelease )
	{
		#ifdef DEBUG
			fputs( "BUTTON RELEASE\n", stderr );
		#endif

    	mouse.subwindow = 0;
	}
}


// configure_request()
//
// Fulfill the request to configure the window
//
// e - The given XEvent

void configure_request( XEvent *e )
{
	#ifdef DEBUG
		fputs( "CONFIGURE REQUEST\n", stderr );
	#endif

    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XConfigureWindow( 
		display, 
		ev->window, 
		ev->value_mask, 
		&(XWindowChanges) {
			.border_width = BORDER,
        	.x            = ev->x,
        	.y            = ev->y,
        	.width        = ev->width,
        	.height       = ev->height,
        	.sibling      = ev->above,
        	.stack_mode   = ev->detail
    	}
	);
}


void destroy_notify( XEvent *e )
{
	#ifdef DEBUG
		fputs("DESTROY NOTIFY\n", stderr );
	#endif

//	window_delete( e->xdestroywindow.event );
}


void enter_notify( XEvent *e )
{
	#ifdef DEBUG
		fputs( "ENTER NOTIFY\n", stderr );
	#endif
	XSetInputFocus(display, e->xcrossing.window, RevertToParent, CurrentTime);
	//window_current( e->xcrossing.window );
}


// key_event()
//
// Respond to key events
//
// e - The given XEvent

void key_event( XEvent *e )
{ 
	#ifdef DEBUG
		fputs( "KEY PRESS\n", stderr );
	#endif

	if( e->type == KeyPress )
	{
		KeySym k = XkbKeycodeToKeysym( display, e->xkey.keycode, 0, 0 );
	
		for( int i = 0; i < LENGTH( KEYS ); i++ )
			if ( ( k == KEYS[i].key ) && ( CLEAN_MASK( KEYS[i].mod ) == CLEAN_MASK( e->xkey.state ) ) ) 
				KEYS[i].f( KEYS[i].a );
	}
	else if( e->type == KeyRelease );
}


// map_request()
//
// Fulfill the request to map the window to the display requesting for window
// entry events and structure change events
//
// e - The given XEvent

void map_request( XEvent *e )
{
	#ifdef DEBUG
		fputs( "MAP REQUEST\n", stderr );
	#endif

	Window window = e->xmaprequest.window;
	
	XSelectInput( display, window, StructureNotifyMask | EnterWindowMask );

	window_add( window );
	XMapWindow( display, window );
	window_current( window );

	window_fullscreen( ( argument_t ) { 0 } );
}


////////////////////////////////////////////////////////////////////////////////
// WINDOW
////////////////////////////////////////////////////////////////////////////////


// window_add()
//
// Add the given window to the workspace
//
// window - The Window to be added

void window_add( Window window )
{
	#ifdef DEBUG
		fputs( "WINDOW ADD\n", stderr );
	#endif

	client_t *c = ( client_t * ) calloc( 1, sizeof( client_t ) );
	c->window = window;
	
	if( workspaces[workspace] )
		c->next = workspaces[workspace];
	
	workspaces[workspace] = c;
	window_current( c->window );
}


// window_delete()
//
// Remove the given window from the workspace
//
// window - The Window to be removed

void window_delete( Window window )
{
	#ifdef DEBUG
		fputs( "WINDOW DELETE\n", stderr );
	#endif

	if( !workspaces[workspace] )
		return;

	client_t *c = workspaces[workspace];

	if( c->window == window )
	{
		free( c );
		workspace[workspaces] = NULL;		
	}
	else
	{
		while( c->next && c->next->window != window )
			c = c->next;

		if( c->next->window == window )
		{
			client_t *r = c->next;
			c->next = c->next->next;

			free( r );
		}
	}
}


// window_kill()
//
// Kill the given window and respective pointer
//
// a - Unused parameter

void window_kill( argument_t const a )
{
	#ifdef DEBUG
		fputs( "WINDOW KILL\n", stderr );
	#endif

    if( workspaces[workspace] ) 
	{
		XKillClient( display, workspaces[workspace]->window );

		client_t *head =  workspaces[workspace]->next;
		free( workspaces[workspace] );
		workspaces[workspace] = head;
		
		if( workspaces[workspace] )
			window_current( workspaces[workspace]->window );
	}
}


// window_current()
//
// Raise, focus and move the given window to front of the client list
//
// window - The Window to be raised and focused

void window_current( Window window )
{
	#ifdef DEBUG
		fputs( "WINDOW CURRENT\n", stderr );
	#endif

	if( !workspaces[workspace] )
		return;

	// Only moves client if it is not at the front
	if( workspaces[workspace]->window != window )
	{
		window_delete( window );
		window_add( window );	
    }

	XSetInputFocus(display, window, RevertToParent, CurrentTime);
	XRaiseWindow( display, window );
}


// window_center()
//
// Center the given window on the screen
//
// window - The Window to be centered

void window_center( Window window )
{
	#ifdef DEBUG
		fputs( "WINDOW CENTER\n", stderr );
	#endif

	uint32_t w, h;
	window_size( window, &(int){0}, &(int){0}, &w, &h );
	XMoveWindow( display, window, (sw - w) / 2, (sh - h) / 2 );
}


// window_fullscreen()
//
// Resize the window to take up the full screen
//
// window - The Window to be resized

void window_fullscreen( argument_t const a )
{
	#ifdef DEBUG
		fputs( "WINDOW FULLSCREEN\n", stderr );
	#endif

	XMoveResizeWindow( 
		display, 
		workspaces[workspace]->window,
	#ifdef GAPS
		GAP_PIXELS,
		GAP_PIXELS,
		sw - GAP_PIXELS * 2,
		sh - GAP_PIXELS * 2
	#else
		0,
		0,
		sw,
		sh
	#endif	
	);
}


void window_next( argument_t const a )
{
	if( !workspaces[workspace] || !workspaces[workspace]->next )
		return;

	client_t *tail = workspaces[workspace];
	workspaces[workspace] = tail->next;
	tail->next = NULL;

	client_t *c = workspaces[workspace];
	while( c->next )
		c = c->next;

	c->next = tail;
	window_current( workspaces[workspace]->window );
}


void window_previous( argument_t const a )
{
	if( !workspaces[workspace] || !workspaces[workspace]->next )
		return;

	client_t *c = workspaces[workspace];
	while( c->next->next )
		c = c->next;

	client_t *head = c->next;
	c->next = NULL;	
	head->next = workspaces[workspace];
	workspaces[workspace] = head;

	window_current( workspaces[workspace]->window );
}


void window_snap( int xf, int yf, int *x, int *y, int *w, int *h )
{
	if( xf )
	{
	#ifdef GAPS
		*w = ( sw / 2 ) - ( GAP_PIXELS * 3.0 / 2.0 );
		*x = ( xf > 0 ) ? ( sw / 2 + GAP_PIXELS / 2.0 ) : GAP_PIXELS;
	#else
		*w = sw / 2;
		*x = ( xf > 0 ) ? sw / 2 : 0;
	#endif
	}		
	else
	{
	#ifdef GAPS
		*w = sw - GAP_PIXELS * 2.0;
		*x = GAP_PIXELS;
	#else
		*w = sw;
		*x = 0;
	#endif
	}

	if( yf )
	{
	#ifdef GAPS
		*h = ( sh / 2 ) - ( GAP_PIXELS * 3.0 / 2.0 );
		*y = ( yf > 0 ) ? ( sh / 2 + GAP_PIXELS / 2.0 ) : GAP_PIXELS;
	#else
		*h = sh / 2;
		*y = ( yf > 0 ) ? sh / 2 : 0;
	#endif
	}
	else
	{
	#ifdef GAPS
		*h = sh - GAP_PIXELS * 2.0;
		*y = GAP_PIXELS;
	#else
		*h = sh;
		*y = 0;
	#endif
	}
}


// 0 - Right
// 1 - Up
// 2 - Left
// 3 - Down

void window_push( argument_t const a )
{
	if( !workspaces[workspace] )
		return;

	int x, y, w, h, xf, yf;	
	window_size( workspaces[workspace]->window, &x, &y, &w, &h );

	if( x < ( sw / 2 ) && w < ( sw / 2 ) )      xf = -1;
	else if( x > ( sw / 2 ) && w < ( sw / 2 ) ) xf = 1;
	else                                        xf = 0;

	if( y < ( sh / 2 ) && h < ( sh / 2 ) )      yf = -1;
	else if( y > ( sh / 2 ) && h < ( sh / 2 ) ) yf = 1;
	else                                        yf = 0;

	fprintf( stderr, "xf: %d\nyf: %d\n\n", xf, yf );
	
	if( ( xf == 1 && a.x == 0 ) || ( xf == -1 && a.x == 2 ) ||
	    ( yf == 1 && a.x == 3 ) || ( yf == -1 && a.x == 1 ) )
		return;

	
	// LEFT / H
	if( a.x == 2 )
	{
		fputs( "2\n", stderr );
		if( xf == 1 )
			xf = 0;

		else
			xf = -1;
	}
	// RIGHT / L
	else if( a.x == 0 )
	{
		fputs( "0\n", stderr );
		if( xf == -1 )
			xf = 0;		

		else
			xf = 1;
	}
	// UP / K
	else if( a.x == 1 )
	{
		fputs( "1\n", stderr );
		if( yf == 1 )
			yf = 0;

		else
			yf = -1;
	}
	// DOWN / J
	else if( a.x == 3 )
	{
		fputs( "3\n", stderr );
		if( yf == -1 )
			yf = 0;

		else
			yf = 1;
	}

	fprintf( stderr, "xf: %d\nyf: %d\n\n", xf, yf );
	fprintf( stderr, "x: %d\ny: %d\nw: %d\nh: %d\n\n", x, y, w, h );

	window_snap( xf, yf, &x, &y, &w, &h );

	fprintf( stderr, "x: %d\ny: %d\nw: %d\nh: %d\n\n", x, y, w, h );

    XMoveResizeWindow(
		display, 
		workspaces[workspace]->window,
        x,
        y,
        w,
        h
	);
}


// window_to_workspace()
//
// Move the window to the given workpace
//
// a.x - The workspace to move the current window to

void window_to_workspace( argument_t const a ) 
{
	#ifdef DEBUG
		fputs( "WINDOW TO WORKSPACE\n", stderr );
	#endif

	if( !workspaces[workspace] || a.x == workspace) 
		return;

	client_t *c = workspaces[workspace];
	workspaces[workspace] = c->next;

	c->next = workspaces[a.x];
	workspaces[a.x] = c;

	XUnmapWindow(display, c->window);

	if( workspaces[workspace] )
		window_current( workspaces[workspace]->window );
}


// to_workspace()
//
// Move to the given workpace
//
// a.x - The workspace to move to

void to_workspace( argument_t const a )
{
	#ifdef DEBUG
		fputs( "TO WORKSPACE\n", stderr );
	#endif

    if (a.x == workspace)
		return;

    client_t *c = workspaces[workspace];

	while( c )
	{
		XUnmapWindow(display, c->window);
		c = c->next;
	}

	workspace = a.x;
	c = workspaces[workspace];

	while( c )
	{
		XMapWindow(display, c->window);
		c = c->next;
	}

    if( workspaces[workspace] ) 
		window_current( workspaces[workspace]->window ); 
} 


// run()
//
// Run the given program
//
// a.p - A pointer to the array of command strings

void run( argument_t const a )
{
	#ifdef DEBUG
		fputs( "RUN\n", stderr );
	#endif

	if( fork() ) 
		return;

    if( display )
		close( ConnectionNumber( display ) );

	if ( errno == EAGAIN )
		fprintf( stderr, "%s\n", "EAGAIN" );
	if ( errno == ENOMEM )
		fprintf( stderr, "%s\n", "ENOMEM" );
	if ( errno == ENOSYS )
		fprintf( stderr, "%s\n", "ENOSYS" );

    setsid();

    execvp( ( char * )( ( char ** ) a.p )[0], ( char ** ) a.p );
}


// quit()
//
// Quit the window manager
//
// a - Unused parameter

void quit( argument_t const a )
{
	loop = 0;
}


// grab_input()
//
// Determine which keys to send events for
//
// d - The indicated display
// root - The root window 

void grab_input()
{	
    uint32_t i, j;
	uint32_t null_modifiers[] = { 
		0, 
		LockMask, 
		NumLockMask, 
		NumLockMask|LockMask
	};

    XModifierKeymap *modmap = XGetModifierMapping( display );

	// NumLock
    for( i = 0; i < 8; i++ )
        for( j = 0; j < modmap->max_keypermod; j++ )
            if( modmap->modifiermap[i * modmap->max_keypermod + j] == 
                 XKeysymToKeycode( display, 0xff7f ) )
                NumLockMask = (1 << i);

	// Keys
    for( i = 0; i < LENGTH(KEYS); i++ )
        for( j = 0; j < LENGTH( null_modifiers ); j++ )
            XGrabKey(
				display, 
				XKeysymToKeycode( display, KEYS[i].key ), 
				KEYS[i].mod | null_modifiers[j], 
				root,    
				True, 
				GrabModeAsync, 
				GrabModeAsync
			);

	// Buttons
    for( i = 1; i < 4; i += 1 )
        for( j = 0; j < LENGTH( null_modifiers); j++ )
            XGrabButton(
				display, 
				i, 
				MOD | null_modifiers[j], 
				root, 
				True,
                ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                GrabModeAsync, 
				GrabModeAsync, 
				0, 
				0
			);

    XFreeModifiermap(modmap);
}


static int xerror()
{ 
	return 0; 
}


int main()
{
    XWindowAttributes attr;
    XEvent ev;

    if( !( display = XOpenDisplay( 0x0 ) ) ) 
		return 1;
    
    signal(SIGCHLD, SIG_IGN);
    XSetErrorHandler(xerror);

	int screen = DefaultScreen( display );
    root = RootWindow( display, screen );
	sw = XDisplayWidth( display, screen );
	sh = XDisplayHeight( display, screen );

	grab_input();

/*
    XGrabButton(dpy, 1, Mod1Mask, root, True, ButtonPressMask, GrabModeAsync,
            GrabModeAsync, None, None);
    XGrabButton(dpy, 3, Mod1Mask, root, True, ButtonPressMask, GrabModeAsync,
            GrabModeAsync, None, None);
*/

	XSelectInput( display, root, SubstructureRedirectMask );
    XDefineCursor( display, root, XCreateFontCursor( display, 68 ) );

	loop = 1;
	while( loop && !XNextEvent( display, &ev ) )
		handle_event( &ev );
}
