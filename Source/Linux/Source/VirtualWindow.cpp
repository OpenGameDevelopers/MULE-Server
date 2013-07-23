#include <VirtualWindow.hpp>

VirtualWindow::VirtualWindow( )
{
}

VirtualWindow::~VirtualWindow( )
{
}

int VirtualWindow::Initialise( )
{
	return 1;
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

