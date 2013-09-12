#ifndef __MULESERVER_VIRTUALWINDOW_HPP__
#define __MULESERVER_VIRTUALWINDOW_HPP__

#include <X11/Xlib.h>
#include <RenderView.hpp>
#include <list>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <GL/glext.h>

typedef struct __tagIMAGE_DATA
{
	unsigned int	ID;
	unsigned char	Data[ 1020 ];
}IMAGE_DATA;

typedef struct __tagIMAGE_LAYOUT
{
	int	Width;
	int Height;
	int Compression;
}IMAGE_LAYOUT;

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

	std::list< RenderView* > m_Views;
	int			m_Socket;
};

#endif

