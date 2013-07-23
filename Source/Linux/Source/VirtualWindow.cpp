#include <VirtualWindow.hpp>
#include <iostream>
#include <dirent.h>
#include <cstring>

VirtualWindow::VirtualWindow( )
{
	m_pDisplay = NULL;
	m_Window = 0;
	m_pXVisualInfo = NULL;
}

VirtualWindow::~VirtualWindow( )
{
	this->Destroy( );
}

int VirtualWindow::Initialise( )
{
	// Open the /tmp/.X11-unix directory and search for files beginning with X
	DIR *pXDir = opendir( "/tmp/.X11-unix" );
	struct dirent *pEntry;

	while( ( pEntry = readdir( pXDir ) ) != NULL )
	{
		std::cout << "/tmp/.X11-unix/" << pEntry->d_name << std::endl;
		if( pEntry->d_name[ 0 ] == 'X' )
		{
			break;
		}
	}

	closedir( pXDir );

	size_t NameLen = strlen( pEntry->d_name );
	char DisplayName[ NameLen+1 ];
	DisplayName[ 0 ] = ':';
	strncpy( DisplayName+1, pEntry->d_name+1, NameLen-1 );
	DisplayName[ NameLen ] = '\0';

	m_pDisplay = XOpenDisplay( DisplayName );

	if( !m_pDisplay )
	{
		std::cout << "Failed to open display: \"" << DisplayName << "\"" <<
			std::endl;
		std::cout << "Name: " << DisplayName << std::endl;
		return 0;
	}

	std::cout << "Opened display: \"" << DisplayName << "\"" << std::endl;

	int VisualAttributes[ ] =
	{
		GLX_X_RENDERABLE,	True,
		GLX_DRAWABLE_TYPE,	GLX_WINDOW_BIT,
		GLX_RENDER_TYPE,	GLX_RGBA_BIT,
		GLX_X_VISUAL_TYPE,	GLX_TRUE_COLOR,
		GLX_RED_SIZE,		8,
		GLX_GREEN_SIZE,		8,
		GLX_BLUE_SIZE,		8,
		GLX_ALPHA_SIZE,		8,
		GLX_DEPTH_SIZE,		24,
		GLX_STENCIL_SIZE,	8,
		GLX_DOUBLEBUFFER,	True,
		None
	};

	int Major = 0, Minor = 0;

	if( !glXQueryVersion( m_pDisplay, &Major, &Minor ) )
	{
		return 0;
	}

	std::cout << "GLX version: Major: " << Major << " | " << "Minor: " <<
		Minor << std::endl;

	int FBCount = 0;
	GLXFBConfig *pFBC = glXChooseFBConfig( m_pDisplay,
		DefaultScreen( m_pDisplay ), VisualAttributes, &FBCount );

	GLXFBConfig FBConfig = pFBC[ 0 ];

	XFree( pFBC );
	pFBC = NULL;

	m_pXVisualInfo = glXGetVisualFromFBConfig( m_pDisplay, FBConfig );

	XSetWindowAttributes WindowAttributes;

	WindowAttributes.colormap = XCreateColormap( m_pDisplay,
		RootWindow( m_pDisplay, m_pXVisualInfo->screen ),
		m_pXVisualInfo->visual, AllocNone );
	WindowAttributes.background_pixmap = None;
	WindowAttributes.border_pixel = 0;
	WindowAttributes.event_mask = StructureNotifyMask | ExposureMask |
		KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
		ResizeRedirectMask | PointerMotionMask | FocusChangeMask |
		EnterWindowMask | LeaveWindowMask;
	WindowAttributes.override_redirect = True;

	m_Window = XCreateWindow( m_pDisplay,
		RootWindow( m_pDisplay, m_pXVisualInfo->screen ), 0, 0, 100, 100, 0,
		m_pXVisualInfo->depth, InputOutput, m_pXVisualInfo->visual,
		CWEventMask | CWColormap | CWBorderPixel | CWOverrideRedirect,
		&WindowAttributes );
	
	GLXContext TestContext = glXCreateContext( m_pDisplay, m_pXVisualInfo, 0,
		True );

	glXMakeCurrent( m_pDisplay, m_Window, TestContext );

	glXMakeCurrent( m_pDisplay, 0, 0 );
	glXDestroyContext( m_pDisplay, TestContext );

	return 1;
}

void VirtualWindow::Destroy( )
{
	if( m_pDisplay )
	{
		XCloseDisplay( m_pDisplay );
	}
}

void VirtualWindow::ProcessEvents( )
{
	XEvent Event;

	int Pending = XPending( m_pDisplay );

	// Maybe create a thread here for network messages?

	for( int i = 0; i < Pending; ++i )
	{
		XNextEvent( m_pDisplay, &Event );

		switch( Event.type )
		{
		}
		// Handle network messages
	}
}

int VirtualWindow::AddView( RenderView &p_View )
{
	m_Views.push_back( &p_View );

	return 1;
}

