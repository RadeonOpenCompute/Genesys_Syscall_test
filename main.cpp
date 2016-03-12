
#include <amp.h>

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

	/* show that we care outside of kernel to prevent DCE */
	int ret;
	parallel_for_each(concurrency::extent<1>(1),
	                  [&](concurrency::index<1> idx) restrict(amp)
	{
		ret = local.send_nonblock(0, {1});
	});
	// Check
//	for (int i = 0; i < WORKITEMS

	return 0;
}
