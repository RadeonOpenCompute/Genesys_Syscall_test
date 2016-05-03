#include <amp.h>
#include <chrono>
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

	unsigned parallel = 1;
	if (argc > 1)
		parallel = ::std::stoi(argv[1]);

	int nonblock = 0;
	if (argc > 2)
		nonblock = 1;
	::std::cout << "Running: " << parallel << (nonblock ? " NON" : " ")
	            << "BLOCK version\n";

	::std::vector<int> ret(parallel);

	auto start = ::std::chrono::high_resolution_clock::now();
	parallel_for_each(concurrency::extent<1>(parallel),
	                  [&](concurrency::index<1> idx) restrict(amp)
	{
		int i = idx[0];
		if (nonblock) {
			do {
				ret[i] = local.send_nonblock(0);
			} while (ret[i] == EAGAIN);
		} else {
			ret[i] = local.send(0);
		}
	});
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	::std::cerr << parallel << ": " << us.count() << std::endl;


	for (size_t i = 0; i < ret.size(); ++i) {
		if (ret[i] != 0)
			::std::cerr << "FAIL at " << i << " ("
			            << ret[i] << ")" << ::std::endl;
	}

	return 0;
}
