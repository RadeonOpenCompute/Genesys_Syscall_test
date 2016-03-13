#include <amp.h>
#include <asm/unistd.h>
#include <iostream>
#include <string>

#include <unistd.h>

#include "amp_syscalls.h"

enum {
	WORKITEMS=64
};

#define ARRAY_SIZE(x)  (sizeof(x) / sizeof(x[0]))

int main(int argc, char **argv)
{
	syscalls::get().init(WORKITEMS);

	/* for some reason we need a reference,
	 * function scope globals are probably broken */
	syscalls &local = syscalls::get();
	::std::string hello[] = {"Hello world from GPU!\n", "Goodbye!\n"};
	uint64_t fds[] = {1,2};

	int parallel = ARRAY_SIZE(hello);
	if (argc > 1)
		parallel = ::std::stoi(argv[1]);

	for (int i = 0; i < parallel; ++i) {
		const ::std::string &s = hello[i % ARRAY_SIZE(hello)];
		::std::cout << "Testing write syscall, the args should be: "
		            << __NR_write << ", " << fds[i % ARRAY_SIZE(fds)]
		            << ", " << (void*)s.c_str() << ", "
		            << s.size() << ::std::endl;
	}

	parallel_for_each(concurrency::extent<1>(parallel),
	                  [&](concurrency::index<1> idx) restrict(amp)
	{
		int i = idx[0];
		const ::std::string &s = hello[i % ARRAY_SIZE(hello)];
		local.send_nonblock(__NR_write, {fds[i % ARRAY_SIZE(fds)],
		                          (uint64_t)s.c_str(), s.size()});
	});
	pid_t p = getpid();
	::std::cout << "My pid is " << p << " Press any key to continue...\n";
	::std::cin.get();

	return 0;
}
