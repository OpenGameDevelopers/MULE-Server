#ifndef __MULESERVER_VIRTUALWINDOW_HPP__
#define __MULESERVER_VIRTUALWINDOW_HPP__

#include <X11/Xlib.h>
#include <RenderView.hpp>
#include <list>
#include <vector>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <GL/glext.h>
#include <netinet/in.h>

typedef struct __tagIMAGE_DATA
{
	unsigned int	ID;
	unsigned char	Data[ 1020 ];
}IMAGE_DATA;

typedef struct __tagIMAGE_LAYOUT
{
	int	Width;
	int	Height;
	int	Compression;
	int	ViewID;
}IMAGE_LAYOUT;

typedef struct __tagIMAGE_DATA_STREAM
{
	int				Offset;
	uint64_t		SequenceNumber;
	unsigned char	Data[ 1008 ];
}IMAGE_DATA_STREAM;

typedef struct __tagREMOTE_CLIENT
{
	int		Socket;
	char	IP[ INET6_ADDRSTRLEN ];
}REMOTE_CLIENT;

const int IMAGE_DATA_HEADER = sizeof( int )*2;

class VirtualWindow
{
public:
	VirtualWindow( );
	~VirtualWindow( );

	int Initialise( );
	void Destroy( );

	void ProcessEvents( );

	int AddView( RenderView &p_View );

private:
	Display		*m_pDisplay;
	Window		m_Window;
	GLXFBConfig	m_GLXFBConfig;
	XVisualInfo	*m_pXVisualInfo;
	GLXContext	m_GLXContext;
	GLint		m_GLVersion[ 2 ];

	std::list< RenderView* >	m_Views;
	int							m_Socket;
	fd_set						m_MasterFDS;
	fd_set						m_ReadFDS;
	int							m_MaximumSocketFD;

	std::vector< REMOTE_CLIENT >	m_Clients;
	// SequenctNumber should be a per-client thing
	uint64_t	m_SequenceNumber;
};

uint64_t htonll( uint64_t p_Host );
uint64_t ntohll( uint64_t p_Network );

#endif

