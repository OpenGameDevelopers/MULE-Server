#include <VirtualWindow.hpp>
#include <iostream>

VirtualWindow::VirtualWindow( )
{
	m_pDisplay = NULL;
	m_Window = 0;
}

VirtualWindow::~VirtualWindow( )
{
	this->Destroy( );
}

int VirtualWindow::Initialise( )
{
	m_pDisplay = XOpenDisplay( "/tmp/.X11-unix/X0" );

	if( m_pDisplay )
	{
		std::cout << "Failed to open display: \"/tmp/.X11-unix/X0\"" <<
			std::endl;
		return 0;
	}

	std::cout << "Opened display: \"/tmp/.X11-unix/X0\"" << std::endl;

	return 1;
}

void VirtualWindow::Destroy( )
{
	if( m_pDisplay )
	{
		XCloseDisplay( m_pDisplay );

		delete m_pDisplay;
		m_pDisplay = NULL;
	}
}

void VirtualWindow::ProcessEvents( )
{
	XEvent Event;

	int Pending = XPending( m_pDisplay );

	for( int i = 0; i < Pending; ++i )
	{
		// Handle X11 events
		// Handle network messages
	}
}

int VirtualWindow::AddView( RenderView &p_View )
{
	m_Views.push_back( &p_View );

	return 1;
}

