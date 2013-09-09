#include <VirtualWindow.hpp>
#include <iostream>
#include <dirent.h>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

const int MAX_BUFFER_LENGTH	= 1024;

void *GetINetAddr( struct sockaddr *p_Addr )
{
	if( p_Addr->sa_family == AF_INET )
	{
		return &( ( ( struct sockaddr_in * )p_Addr )->sin_addr );
	}

	return &( ( ( struct sockaddr_in6 * )p_Addr )->sin6_addr );
}

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
	// Create the server part
	struct addrinfo SocketHints, *pServerInfo, *pAddrItr;
	memset( &SocketHints, 0, sizeof( SocketHints ) );
	SocketHints.ai_family	= AF_UNSPEC;
	SocketHints.ai_socktype	= SOCK_DGRAM;
	SocketHints.ai_flags	= AI_PASSIVE;

	int Error;

	if( ( Error = getaddrinfo( NULL, "5092", &SocketHints, &pServerInfo ) )
		!= 0 )
	{
		printf( "getaddrinfo: %s\n", gai_strerror( Error ) );
		return 0;
	}

	for( pAddrItr = pServerInfo; pAddrItr != NULL;
		pAddrItr = pAddrItr->ai_next )
	{
		if( ( m_Socket = socket( pAddrItr->ai_family, pAddrItr->ai_socktype,
			pAddrItr->ai_protocol ) ) == -1 )
		{
			printf( "Error on creating a socket\n" );
			continue;
		}

		if( bind( m_Socket, pAddrItr->ai_addr, pAddrItr->ai_addrlen ) == -1 )
		{
			close( m_Socket );
			printf( "Error on binding socket\n" );
			continue;
		}

		break;
	}

	freeaddrinfo( pServerInfo );

	if( pAddrItr == NULL )
	{
		printf( "Failed to create a socket to listen on\n" );
		return 0;
	}

	// Open the /tmp/.X11-unix directory and search for files beginning with X
	DIR *pXDir = opendir( "/tmp/.X11-unix" );
	struct dirent *pEntry;

	while( ( pEntry = readdir( pXDir ) ) != NULL )
	{
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

	// The window size does not matter, as it will not be shown
	m_Window = XCreateWindow( m_pDisplay,
		RootWindow( m_pDisplay, m_pXVisualInfo->screen ), 0, 0, 800, 600, 0,
		m_pXVisualInfo->depth, InputOutput, m_pXVisualInfo->visual,
		CWEventMask | CWColormap | CWBorderPixel | CWOverrideRedirect,
		&WindowAttributes );
	
/*	GLXContext TestContext = glXCreateContext( m_pDisplay, m_pXVisualInfo, 0,
		True );*/
	m_GLXContext = glXCreateContext( m_pDisplay, m_pXVisualInfo, 0, True );

	glXMakeCurrent( m_pDisplay, m_Window, m_GLXContext /*TestContext*/ );

	char *pGLVersion = ( char * )glGetString( GL_VERSION );

	std::cout << "OpenGL version [string]: " << pGLVersion << std::endl;

	char *pTokenVersion = strtok( pGLVersion, " ." );

	for( int i = 0; i < 2; ++i )
	{
		m_GLVersion[ i ] = atoi( pTokenVersion );
		pTokenVersion = strtok( NULL, ". " );
	}

	std::cout << "OpenGL version [decimal]: " << m_GLVersion[ 0 ] << "." <<
		m_GLVersion[ 1 ] << std::endl;
	
/*	glXMakeCurrent( m_pDisplay, 0, 0 );
	glXDestroyContext( m_pDisplay, TestContext );*/

/*	if( ( m_GLVersion[ 0 ] < 3 ) ||
		( m_GLVersion[ 0 ] == 3 && m_GLVersion[ 1 ] < 2 ) )
	{
		std::cout << "Failed to create an OpenGL 3.2 or greater context" <<
			std::endl;
		return 0;
	}*/

	glClearColor( 0.4f, 0.0f, 0.0f, 1.0f );
	glViewport( 0, 0, 800, 600 );

	return 1;
}

void VirtualWindow::Destroy( )
{
	if( m_Socket )
	{
		close( m_Socket );
	}

	if( m_GLXContext )
	{
		glXMakeCurrent( m_pDisplay, 0, 0 );
		glXDestroyContext( m_pDisplay, m_GLXContext );
	}

	if( m_pXVisualInfo )
	{
		XFree( m_pXVisualInfo );
	}

	if( m_Window )
	{
		XDestroyWindow( m_pDisplay, m_Window );
	}

	if( m_pDisplay )
	{
		XCloseDisplay( m_pDisplay );
	}
}

void VirtualWindow::ProcessEvents( )
{
	XEvent Event;
	static int FramesSent = 0;
	struct sockaddr_storage RemoteAddress;
	socklen_t AddressLength;
	char Buffer[ MAX_BUFFER_LENGTH ];
	int BytesRecv;
	int NumBytes;
	char AddrStr[ INET6_ADDRSTRLEN ];
	int BytesToGo = 3*800*600;
	char SendBuffer[ 1024 ];

	int Pending = XPending( m_pDisplay );

	// Maybe create a thread here for network messages?

	for( int i = 0; i < Pending; ++i )
	{
		XNextEvent( m_pDisplay, &Event );

		switch( Event.type )
		{
		}
	}

	// Handle network messages and render the world
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	// Get the buffer here?
	glXSwapBuffers( m_pDisplay, m_Window );

	printf( "Waiting on client to send frame data...\n" );

	AddressLength = sizeof( RemoteAddress );

	if( ( BytesRecv = recvfrom( m_Socket, Buffer, MAX_BUFFER_LENGTH-1, 0,
		( struct sockaddr * )&RemoteAddress, &AddressLength ) ) == -1 )
	{
		printf( "Error receiving from socket\n" );
	}
	else
	{
		printf( "Got packet from %s\n", inet_ntop( RemoteAddress.ss_family,
			GetINetAddr( ( struct sockaddr * )&RemoteAddress ), AddrStr,
			sizeof( AddrStr ) ) );
		printf( "Packet [%d bytes]:\n", BytesRecv );
		Buffer[ BytesRecv ] = '\0';
		printf( "%s\n", Buffer );
		int BufferPos = 0;

		while( BytesToGo > 0 )
		{
			memset( SendBuffer, 0xFF, MAX_BUFFER_LENGTH );
			BytesToGo -= MAX_BUFFER_LENGTH;
			BufferPos += MAX_BUFFER_LENGTH;

			printf( "Bytes to go:     %d\n", BytesToGo );
			printf( "Buffer position: %d\n", BufferPos );
			
			if( ( NumBytes = sendto( m_Socket, SendBuffer,
				MAX_BUFFER_LENGTH, 0,
				( struct sockaddr * )&RemoteAddress, AddressLength ) ) == -1 )
			{
				printf( "Failed to send data\n" );
			}
			else
			{
				printf( "Sent data\n" );
			}
		}
		FramesSent++;
		printf( "Sent %d frames\n", FramesSent );
	}
}

int VirtualWindow::AddView( RenderView &p_View )
{
	m_Views.push_back( &p_View );

	return 1;
}

