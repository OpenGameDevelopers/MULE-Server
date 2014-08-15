#include <VirtualWindow.hpp>
#include <iostream>
#include <dirent.h>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
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
unsigned long g_JPEGBufferLength = 0UL;

static int g_ViewID = 1;

void *GetINetAddr( struct sockaddr *p_Addr )
{
	if( p_Addr->sa_family == AF_INET )
	{
		return &( ( ( struct sockaddr_in * )p_Addr )->sin_addr );
	}

	return &( ( ( struct sockaddr_in6 * )p_Addr )->sin6_addr );
}

uint64_t htonll( uint64_t p_Host )
{
	return ( ( ( uint64_t ) htonl( p_Host ) << 32 ) + htonl( p_Host >> 32 ) );
}

uint64_t ntohll( uint64_t p_Network )
{
	return ( ( ( uint64_t ) ntohl( p_Network ) << 32 ) +
		ntohl( p_Network >> 32 ) );
}

VirtualWindow::VirtualWindow( )
{
	m_pDisplay = NULL;
	m_Window = 0;
	m_pXVisualInfo = NULL;
	memset( g_BufferToSend, 0, IMAGE_WIDTH*IMAGE_HEIGHT*IMAGE_CHANNELS );
	m_SequenceNumber = 0ULL;
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
	SocketHints.ai_socktype	= SOCK_STREAM;
	SocketHints.ai_protocol	= IPPROTO_TCP;
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

	if( listen( m_Socket, 128 ) == -1 )
	{
		printf( "Failed to set socket to listen\n" );
		return 0;
	}

	int NonBlock = 1;
	if( fcntl( m_Socket, F_SETFL, O_NONBLOCK, NonBlock ) == -1 )
	{
		printf( "Failed to set up non-blocking socket\n" );
	}

	FD_ZERO( &m_MasterFDS );
	FD_ZERO( &m_ReadFDS );
	FD_SET( m_Socket, &m_MasterFDS );

	m_MaximumSocketFD = m_Socket;

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
	static size_t LastClientCount = 0;
	XEvent Event;
	static bool JpegGen = false;
	static int FramesSent = 0;
	struct sockaddr_storage RemoteAddress;
	socklen_t AddressLength;
	IMAGE_DATA Buffer;
	int BytesRecv;
	int NumBytes;
	char AddrStr[ INET6_ADDRSTRLEN ];
	long BytesToGo = 0L;
	IMAGE_LAYOUT Layout;
	memset( &Layout, 0, sizeof( Layout ) );

	int Pending = XPending( m_pDisplay );

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

	if( m_Clients.size( ) != LastClientCount )
	{
		if( m_Clients.size( ) > 0 )
		{
			for( size_t i = 0; i < m_Clients.size( ); ++i )
			{
				printf( "%d clients currently connected:\n", m_Clients.size( ) );
				for( int i = 0; i < m_Clients.size( ); ++i )
				{
					printf( "\t%s\n", m_Clients[ i ].IP );
				}
			}
		}
		else
		{
			printf( "Waiting on client\n" );
		}

		LastClientCount = m_Clients.size( );
	}

	memcpy( &m_ReadFDS, &m_MasterFDS, sizeof( m_MasterFDS ) );

	int SocketsReady = 0;

	struct timeval TimeOut;
	TimeOut.tv_sec = 0;
	TimeOut.tv_usec = 0;

	if( -1 == ( SocketsReady = select( m_MaximumSocketFD + 1, &m_ReadFDS,
		NULL, NULL, &TimeOut ) ) )
	{
		printf( "Could not call select( ) on socket: %s\n", strerror( errno ) );
	}

	for( int i = 0; i <= m_MaximumSocketFD && SocketsReady > 0; ++i )
	{
		if( FD_ISSET( i, &m_ReadFDS ) )
		{
			printf( "Accepting...\n" );
			--SocketsReady;

			if( i == m_Socket )
			{
				int NewClient = 0;
				printf( "New client\n" );

				struct sockaddr_storage ClientAddr;
				socklen_t Size = sizeof( ClientAddr );

				if( -1 == ( NewClient = accept( m_Socket, 
					( struct sockaddr * )&ClientAddr, &Size ) ) )
				{
					if( EWOULDBLOCK != errno )
					{
						printf( "Failed calling accept( )\n" );
						break;
					}
				}

				FD_SET( NewClient, &m_MasterFDS );

				if( NewClient > m_MaximumSocketFD )
				{
					m_MaximumSocketFD = NewClient;
				}

				char IP[ INET6_ADDRSTRLEN ];

				inet_ntop( ClientAddr.ss_family,
					GetINetAddr( ( struct sockaddr * )&ClientAddr ),
					IP, sizeof( IP ) );

				REMOTE_CLIENT Remote;
				memset( &Remote, 0, sizeof( Remote ) );
				Remote.Socket = NewClient;
				strncpy( Remote.IP, IP, strlen( IP ) );

				m_Clients.push_back( Remote );

				printf( "\tIP address: %s\n", Remote.IP );
			}
			else
			{
				printf( "Receiving data..\n" );

				if( ( BytesRecv = recv( i, &Buffer, sizeof( Buffer ), 0 ) ) ==
					-1 )
				{
					if( EWOULDBLOCK != errno )
					{
						printf( "Failed to receive data\n" );
					}
					break;
				}

				if( BytesRecv == 0 )
				{
					printf( "Client closed connection\n" );
					close( i );
					FD_CLR( i, &m_MasterFDS );
					
					std::vector< REMOTE_CLIENT >::iterator ClientDisconnected;
					ClientDisconnected = m_Clients.begin( );
					for( ; ClientDisconnected != m_Clients.end( );
						++ClientDisconnected )
					{
						if( ( *ClientDisconnected ).Socket == i )
						{
							break;
						}
					}

					if( ClientDisconnected != m_Clients.end( ) )
					{
						m_Clients.erase( ClientDisconnected );
					}
					break;
				}

				printf( "Packet type: %d\n", ntohl( Buffer.ID ) );
				switch( ntohl( Buffer.ID ) )
				{
					case 1:
					{
						memcpy( &Layout, Buffer.Data, sizeof( Layout ) );
						Layout.Width = ntohl( Layout.Width );
						Layout.Height = ntohl( Layout.Height );
						Layout.Compression = ntohl( Layout.Compression );
						Layout.ViewID = ntohl( Layout.ViewID );

						printf( "=== LAYOUT ===\n" );
						printf( "View ID: %d\n", Layout.ViewID );
						printf( "Width: %d\n", Layout.Width );
						printf( "Height: %d\n", Layout.Height );
						printf( "Compression: %d\n", Layout.Compression );
						printf( "==============\n" );
						break;
					}
					case 2:
					{
						IMAGE_DATA ViewID;
						ViewID.ID = htonl( 0 );
						int ViewIDNet = htonl( g_ViewID );
						memcpy( ViewID.Data, &ViewIDNet, sizeof( int ) );
						send( i, &ViewID, sizeof( ViewID ), 0 );

						printf( "Sending client a new view ID: %d\n",
							g_ViewID );
						++g_ViewID;
						break;
					}
					case 3:
					{
						BytesToGo = g_JPEGBufferLength;

						printf( "Total bytes to send: %d\n", BytesToGo );

						unsigned long PacketsLeft =
							( BytesToGo / ( sizeof( Buffer )-(
								IMAGE_DATA_HEADER + IMAGE_DATA_STREAM_HEADER )
								)  ) +
							( ( BytesToGo % ( sizeof( Buffer )-(
								IMAGE_DATA_HEADER + IMAGE_DATA_STREAM_HEADER )
								) ) ? 1 : 0 );

						printf( "Total packets to send: %lu\n", PacketsLeft );
						unsigned int IDProcessed[ PacketsLeft ];
						memset( IDProcessed, 0, sizeof( IDProcessed ) );

						IMAGE_DATA_STREAM Stream;
						memset( &Stream, 0, sizeof( Stream ) );

						unsigned long PacketCount = htonll( PacketsLeft );
						Buffer.ID = htonl( 1 );

						unsigned long BufferSize = htonll( g_JPEGBufferLength );
						
						memcpy( Buffer.Data, &PacketCount,
							sizeof( unsigned long ) );
						memcpy( Buffer.Data + sizeof( unsigned long ),
							&BufferSize, sizeof( unsigned long ) );

						send( i, &Buffer, sizeof( Buffer ), 0 );

						printf( "Okay\n" );

						while( PacketsLeft > 0 )
						{
							printf( "Sending...\n" );
							int BufferPos = 0;
							const int BytesLeft = BytesToGo % 
								( MAX_BUFFER_LENGTH-( IMAGE_DATA_HEADER +
									IMAGE_DATA_STREAM_HEADER ) );

							printf( "Data subtracted: %d\n", 
								( MAX_BUFFER_LENGTH-( IMAGE_DATA_HEADER +
									IMAGE_DATA_STREAM_HEADER ) ) );

							if( BytesLeft )
							{
								printf( "Left: %d\n", BytesLeft );
								BytesToGo -= BytesLeft;
								BufferPos += BytesLeft;
								Buffer.ID = htonl( 2 );
								Stream.Offset = htonl( BytesLeft );
								Stream.SequenceNumber = m_SequenceNumber;
								memcpy( Stream.Data, g_pJPEGBuffer,
									BytesLeft );
								memcpy( Buffer.Data, &Stream,
									sizeof( Stream ) );

								if( ( NumBytes = send( i, &Buffer,
									sizeof( Buffer ), 0 ) ) == -1 )
								{
									printf( "Failed to send leftovers\n" );
								}
								else
								{
									printf( "Leftovers sent\n" );
								}

								// Assume the packet went through...
								--PacketsLeft;
							}

							printf( "Bytes to go: %d\n", BytesToGo );

							while( BytesToGo > 0 )
							{
								Buffer.ID = htonl( 3 );
								Stream.Offset = htonl( BufferPos );
								memcpy( Stream.Data, g_pJPEGBuffer + BufferPos,
									sizeof( Stream.Data ) );
								memcpy( Buffer.Data, &Stream,
									sizeof( Stream ) );

								BytesToGo -= sizeof( Stream.Data );
								BufferPos += sizeof( Stream.Data );

								printf( "Bytes to go:     %d\n", BytesToGo );
								printf( "Buffer position: %d\n", BufferPos );

								if( ( NumBytes = send( i, &Buffer,
									sizeof( Buffer ), 0 ) ) == -1 )
								{
									printf( "Failed to send data: %s\n",
										strerror( errno ) );
								}
								else
								{
								}

								// Assume the packet went through...
								--PacketsLeft;
							}

							printf( "Packets remaining: %d\n", PacketsLeft );
						}
						++m_SequenceNumber;
						printf( "Sequence Number: %llu\n", m_SequenceNumber );
						break;
					}
					default:
					{
						printf( "Unknown ID\n" );
						break;
					}
				}
			}
		}
	}

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

	// Still need to extract the framebuffer and turn it into a JPEG
}

int VirtualWindow::AddView( RenderView &p_View )
{
	m_Views.push_back( &p_View );

	return 1;
}

