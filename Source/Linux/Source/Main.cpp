#include <iostream>
#include <GitVersion.hpp>

int main( int p_Argc, char **p_ppArgv )
{
	std::cout << "MULE [Server]" << std::endl;
	std::cout << "Build information:" << std::endl;
	std::cout << "\tDate: " << GIT_COMMITTERDATE << std::endl;
	std::cout << "\tHash: " << GIT_COMMITHASH << std::endl;
	return 1;
}

