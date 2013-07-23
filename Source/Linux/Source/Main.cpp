#include <iostream>
#include <GitVersion.hpp>
#include <VirtualWindow.hpp>

int main( int p_Argc, char **p_ppArgv )
{
	std::cout << "MULE [Server]" << std::endl;
	std::cout << "Build information:" << std::endl;
	std::cout << "\tDate: " << GIT_COMMITTERDATE << std::endl;
	std::cout << "\tHash: " << GIT_COMMITHASH << std::endl;

	VirtualWindow VirtuaWin;
	if( !VirtuaWin.Initialise( ) )
	{
		std::cout << "Failed to initialise one virtual window" << std::endl;
		return 0;
	}

	while( 1 )
	{
		VirtuaWin.ProcessEvents( );
	}

	return 1;
}

