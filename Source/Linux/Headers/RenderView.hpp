#ifndef __MULESERVER_RENDERVIEW_HPP__
#define __MULESERVER_RENDERVIEW_HPP__

#include <GL/gl.h>

class RenderView
{
public:
	RenderView( );
	~RenderView( );
	int		Initialise( );
	void	Update( );
private:
	GLint	m_ID;
	int 	m_Width;
	int 	m_Height;
};

#endif

