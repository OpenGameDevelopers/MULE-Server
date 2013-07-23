#ifndef __MULESERVER_VIRTUALWINDOW_HPP__
#define __MULESERVER_VIRTUALWINDOW_HPP__

#include <X11/Xlib.h>
#include <RenderView.hpp>
#include <list>

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
	Display	*m_pDisplay;
	Window	m_Window;

	std::list< RenderView* > m_Views;
};

#endif

