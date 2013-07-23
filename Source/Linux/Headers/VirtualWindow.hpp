#ifndef __MULESERVER_VIRTUALWINDOW_HPP__
#define __MULESERVER_VIRTUALWINDOW_HPP__

#include <X11/Xlib.h>

class VirtualWindow
{
public:
	explicit VirtualWindow( const int p_Width, const int p_Height );
	~VirtualWindow( );

	int ResizeWindow( const int p_Width, const int p_Height );

private:
	Display	*m_pDisplay;
	Window	*m_Window;
};

#endif

