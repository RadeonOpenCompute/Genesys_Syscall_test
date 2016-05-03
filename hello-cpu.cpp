#include <amp.h>
#include <deque>
#include <iostream>
#include <string>

#include <asm/unistd.h>
#include <unistd.h>

#include "amp_syscalls.h"

int main(int argc, char **argv)
{

	/* for some reason we need a reference,
	 * function scope globals are probably broken */
	syscalls &local = syscalls::get();
	::std::deque<::std::string> hello;
	if (argc <= 2)
		hello.push_back("Hello World from GPU!\n");
	for (int i = 2; i < argc; ++i) {
		hello.push_back(::std::string(argv[i]) + "\n");
	}

	int parallel = hello.size();
	if (argc > 1)
		parallel = ::std::stoi(argv[1]);
#ifdef VERBOSE
	for (int i = 0; i < parallel; ++i) {
		const ::std::string &s = hello[i % hello.size()];
		::std::cout << "Testing write syscall, the args should be: "
		            << __NR_write << ", " << 1
		            << ", " << (void*)s.c_str() << ", "
		            << s.size() << ::std::endl;
	}
#endif

	::std::vector<int> ret(parallel);
	auto start = ::std::chrono::high_resolution_clock::now();
	for (unsigned i = 0; i < parallel; ++i)
	{
		const ::std::string &s = hello[i % hello.size()];
		ret[i] = write( 1, s.c_str(), s.size());
	};
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	::std::cerr << parallel << ": " << us.count() << std::endl;
#ifdef VERBOSE
	pid_t p = getpid();
	for (size_t i = 0; i < ret.size(); ++i)
		::std::cout << "Ret (" << i << "): " << ret[i] << "\n";
	::std::cout << "My pid is " << p << " Press any key to continue...\n";
	::std::cin.get();
#endif

	return 0;
}
