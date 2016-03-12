#include <amp.h>
#include <asm/unistd.h>
#include <iostream>
#include <string>

#include <unistd.h>

#include "amp_syscalls.h"

enum {
	WORKITEMS=64
};

int main(void)
{
	syscalls::get().init(WORKITEMS);

	/* for some reason we need a reference,
	 * function scope globals are probably broken */
	syscalls &local = syscalls::get();
	::std::string hello = "Hello world from GPU!\n";
	const uint64_t text = (uint64_t)hello.c_str();
	const size_t length = hello.size();

	::std::cout << "Testing write syscall, the args should be: "
	            << __NR_write << ", " << (void*)text << ", " << length
	            << ::std::endl;

	/* show that we care outside of kernel to prevent DCE */
	int ret;
	parallel_for_each(concurrency::extent<1>(1),
	                  [&](concurrency::index<1> idx) restrict(amp)
	{
		ret = local.send_nonblock(__NR_write, {1, text, length});
	});
	pid_t p = getpid();
	::std::cout << "My pid is " << p << " Press any key to continue...\n";
	::std::cin.get();

	return 0;
}
