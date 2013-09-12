#include <VirtualWindow.hpp>
#include <iostream>
#include <dirent.h>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <jpeglib.h>

const int MAX_BUFFER_LENGTH	= 1024;

typedef struct __tagImagePacket
{
//	int		BlockIndex;
	int		Offset;
	char	Data[ 1020 ];
}ImagePacket;

typedef struct __tagColour
{
	char R;
	char G;
	char B;
}Colour;

const int PACKET_HEADER		= sizeof( int );//*2;
const int IMAGE_WIDTH		= 800;
const int IMAGE_HEIGHT		= 600;
// Blocks will split the image up, anything that doesn't fill in the pitch is
// ignored
const int BLOCK_SIZE		= 32;
const int IMAGE_CHANNELS	= 3;

const int BLOCK_COLUMNS	= IMAGE_WIDTH/BLOCK_SIZE +
			( IMAGE_WIDTH%BLOCK_SIZE ? 1 : 0 );
const int BLOCK_ROWS = IMAGE_HEIGHT/BLOCK_SIZE +
			( IMAGE_HEIGHT%BLOCK_SIZE ? 1 : 0 );

char g_BufferToSend[ IMAGE_WIDTH*IMAGE_HEIGHT*IMAGE_CHANNELS ];
Colour g_Block[ BLOCK_COLUMNS * BLOCK_ROWS ]
	[ BLOCK_SIZE*BLOCK_SIZE*IMAGE_CHANNELS ];

unsigned char *g_pJPEGBuffer = NULL;
unsigned long g_JPEGBufferLength = 0;

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
	memset( g_BufferToSend, 0, IMAGE_WIDTH*IMAGE_HEIGHT*IMAGE_CHANNELS );
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

		if( ( bind( m_Socket, pAddrItr->ai_addr, pAddrItr->ai_addrlen ) )
			== -1 )
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

/*	int NonBlock = 1;
	if( fcntl( m_Socket, F_SETFL, O_NONBLOCK, NonBlock ) == -1 )
	{
		printf( "Failed to set up non-blocking socket\n" );
	}*/

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

	// Set up the buffer which will just be a vertical grey gradient
	// The next step after this is to use the OpenGL back buffer
	for( int r = 0; r < IMAGE_HEIGHT; ++r )
	{
		for( int i = 0; i < IMAGE_WIDTH*IMAGE_CHANNELS; ++i )
		{
			g_BufferToSend[ i+( r*IMAGE_WIDTH*IMAGE_CHANNELS ) ] = ( r%256 ) & 0xFF;
		}
	}

	char Scale = 0;
	for( int Row = 0; Row < BLOCK_ROWS; ++Row )
	{
		for( int Col = 0; Col < BLOCK_COLUMNS; ++Col )
		{
			for( int i = 0; i < BLOCK_SIZE*BLOCK_SIZE; ++i )
			{
				g_Block[ Row*Col ][ i ].R = 0xFF;//Scale;
				g_Block[ Row*Col ][ i ].G = 0xFF;//Scale;
				g_Block[ Row*Col ][ i ].B = 0xFF;// Scale;
			}
		}
		++Scale;
	}
/*
	FILE *tmp = fopen( "buffer", "wb" );
	for( int i = 0; i < 800*600; ++i )
	{
		if( ( i % 800 ) == 0 )
		{
			fprintf( tmp, "\n" );
		}
		fprintf( tmp, "%02X ", g_BufferToSend[ i ] );
	}
	fclose( tmp );*/

	return 1;
}

void VirtualWindow::Destroy( )
{
	if( g_pJPEGBuffer )
	{
		free( g_pJPEGBuffer );
	}
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
	static bool JpegGen = false;
	static int FramesSent = 0;
	struct sockaddr_storage RemoteAddress;
	socklen_t AddressLength;
	IMAGE_DATA Buffer;
	int BytesRecv;
	int NumBytes;
	char AddrStr[ INET6_ADDRSTRLEN ];
	int BytesToGo = IMAGE_WIDTH*IMAGE_HEIGHT*IMAGE_CHANNELS;
	IMAGE_LAYOUT Layout;
	memset( &Layout, 0, sizeof( Layout ) );

	int Pending = XPending( m_pDisplay );

	printf( "Waiting on client\n" );
	if( ( BytesRecv = recvfrom( m_Socket, &Buffer, sizeof( Buffer ), 0,
		( struct sockaddr * )&RemoteAddress, &AddressLength ) ) == -1 )
	{
	}
	else
	{
		printf( "Packet type: %d\n", ntohl( Buffer.ID ) );
		switch( ntohl( Buffer.ID ) )
		{
			case 1:
			{
				memcpy( &Layout, Buffer.Data, sizeof( Layout ) );
				Layout.Width = ntohl( Layout.Width );
				Layout.Height = ntohl( Layout.Height );
				Layout.Compression = ntohl( Layout.Compression );
				printf( "Width: %d\n", Layout.Width );
				printf( "Height: %d\n", Layout.Height );
				printf( "Compression: %d\n", Layout.Compression );
				break;
			}
			default:
			{
				printf( "Unknown ID\n" );
				break;
			}
		}
	}
	printf( "Client connected\n" );

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

	if( JpegGen == false )
	{
		printf( "Writing image... " );

		FILE *pOut = fopen( "/tmp/TestMULE.jpg", "wb" );

		if( !pOut )
		{
			printf( "Failed to open /tmp/TestMULE.jpg for writing" );
			JpegGen = true;
			return;
		}

		struct jpeg_compress_struct JpegInfo;
		struct jpeg_error_mgr		JpegError;
		JpegInfo.err = jpeg_std_error( &JpegError );
		jpeg_create_compress( &JpegInfo );
		jpeg_mem_dest( &JpegInfo, &g_pJPEGBuffer, &g_JPEGBufferLength );
		JpegInfo.image_width = IMAGE_WIDTH;
		JpegInfo.image_height = IMAGE_HEIGHT;
		JpegInfo.input_components = IMAGE_CHANNELS;
		JpegInfo.in_color_space = JCS_RGB;

		jpeg_set_defaults( &JpegInfo );
		jpeg_set_quality( &JpegInfo, 100, true );
		jpeg_start_compress( &JpegInfo, true );
		JSAMPROW pRow;

		// Need to flip the image
		while( JpegInfo.next_scanline < JpegInfo.image_height )
		{
			pRow = ( JSAMPROW )&g_BufferToSend[ JpegInfo.next_scanline *
				IMAGE_CHANNELS * IMAGE_WIDTH ];
			jpeg_write_scanlines( &JpegInfo, &pRow, 1 );
		}

		jpeg_finish_compress( &JpegInfo );
		jpeg_destroy_compress( &JpegInfo );
		printf( "done\n" );

		printf( "Total image size: %lu\n", g_JPEGBufferLength );

		fwrite( g_pJPEGBuffer, sizeof( unsigned char ), g_JPEGBufferLength,
			pOut );
		
		fclose( pOut );

		JpegGen = true;
	}
/*
	BytesToGo = g_JPEGBufferLength;

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
		
		printf( "Bytes per block: %d\n",
			BLOCK_SIZE*BLOCK_SIZE*IMAGE_CHANNELS );
		printf( "%d packets of size %d\n",
			( BLOCK_SIZE*BLOCK_SIZE*IMAGE_CHANNELS ) /
				( sizeof( ImagePacket )-PACKET_HEADER ),
				sizeof( ImagePacket )-PACKET_HEADER);
		printf( "One packet of size %d\n",
			( BLOCK_SIZE*BLOCK_SIZE*IMAGE_CHANNELS ) %
				( sizeof( ImagePacket )-PACKET_HEADER ) );
		
		int BufferPos = 0;
		const int BytesLeft = BytesToGo % ( MAX_BUFFER_LENGTH-PACKET_HEADER );

		ImagePacket TmpPkt;
		memset( &TmpPkt, 0, sizeof( TmpPkt ) );
		int Counter = 1;

		if( BytesLeft )
		{
			printf( "Left: %d\n", BytesLeft );
			BytesToGo -= BytesLeft;
			BufferPos += BytesLeft;

			memcpy( TmpPkt.Data, g_pJPEGBuffer, BytesLeft );
			TmpPkt.Offset = htonl( 0 );

			if( ( NumBytes = sendto( m_Socket, &TmpPkt,
				BytesLeft, 0, ( struct sockaddr * )&RemoteAddress,
				AddressLength ) ) == -1 )
			{
				printf( "Failed to send leftovers\n" );
			}
			else
			{
				printf( "Leftovers sent\n" );
			}
			++Counter;
		}

		printf( "Bytes to go: %d\n", BytesToGo );

		while( BytesToGo > 0 )
		{
			TmpPkt.Offset = htonl( BufferPos );
			memcpy( TmpPkt.Data, g_pJPEGBuffer + BufferPos,
				MAX_BUFFER_LENGTH - PACKET_HEADER );

			BytesToGo -= ( MAX_BUFFER_LENGTH - PACKET_HEADER );
			BufferPos += ( MAX_BUFFER_LENGTH - PACKET_HEADER );

			printf( "Bytes to go:     %d\n", BytesToGo );
			printf( "Buffer position: %d\n", BufferPos );

			if( ( NumBytes = sendto( m_Socket, &TmpPkt,
				MAX_BUFFER_LENGTH, 0,
				( struct sockaddr * )&RemoteAddress, AddressLength ) ) == -1 )
			{
				printf( "Failed to send data: %s\n", strerror( errno ) );
			}
			else
			{
				// printf( "Sent %d bytes\n", NumBytes );
			}
			++Counter;
		}
		FramesSent++;
		printf( "Sent %d frames | %d packets\n", FramesSent, Counter );*/
	/*	
		for( int i = 0; i < BLOCK_COLUMNS*BLOCK_ROWS; ++i )
		{
			ImagePacket TmpPkt;
			int BufferOffset = 0;
			memset( &TmpPkt, 0, sizeof( TmpPkt ) );
			TmpPkt.BlockIndex = htonl( i );

			int BytesRemaining =
				( BLOCK_SIZE*BLOCK_SIZE*IMAGE_CHANNELS );
			const int BytesLeft =
				BytesRemaining %( sizeof( ImagePacket )-PACKET_HEADER );

			if( BytesLeft )
			{
				memcpy( TmpPkt.Data, g_Block[ i ], BytesLeft );
				TmpPkt.Offset = htonl( 0 );

				if( ( NumBytes = sendto( m_Socket, &TmpPkt,
					BytesLeft, 0, ( struct sockaddr * )&RemoteAddress,
					AddressLength ) ) == -1 )
				{
					printf( "Failed to send leftovers\n" );
				}

				BufferOffset += BytesLeft;
				BytesRemaining -= BytesLeft;
			}

			while( BytesRemaining > 0 )
			{
				TmpPkt.Offset = htonl( BufferOffset );

				memcpy( TmpPkt.Data, g_Block[ i ] + BufferOffset,
					sizeof( ImagePacket ) - PACKET_HEADER );

				if( ( NumBytes = sendto( m_Socket, &TmpPkt,
					sizeof( ImagePacket ), 0,
					( struct sockaddr * )&RemoteAddress, AddressLength ) ) ==
						-1 )
				{
					printf( "Failed to send data\n" );
				}

				BufferOffset += ( sizeof( ImagePacket ) - PACKET_HEADER );
				BytesRemaining -= ( sizeof( ImagePacket ) - PACKET_HEADER );
			}
		}*/
	//}
}

int VirtualWindow::AddView( RenderView &p_View )
{
	m_Views.push_back( &p_View );

	return 1;
}

