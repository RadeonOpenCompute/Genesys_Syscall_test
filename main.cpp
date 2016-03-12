#include <iostream>
#include <hsakmt.h>
#include <linux/kfd_sc.h>

#include <amp.h>
using namespace concurrency;

enum {
	WORKITEMS=64
};

class syscalls {
public:
	kfd_sc *syscalls_ = NULL;
	size_t elements_ = 0;
	int init(size_t elements);
	static syscalls& get() restrict (amp,cpu)
	{
		static syscalls instance;
		return instance;
	}
	int send(int sc,
		uint64_t param0 = 0, uint64_t param1 = 0, uint64_t param2 = 0,
		uint64_t param3 = 0, uint64_t param4 = 0, uint64_t param5 = 0)
	restrict(amp);
	int send_nonblock(int sc,
		uint64_t param0 = 0, uint64_t param1 = 0, uint64_t param2 = 0,
		uint64_t param3 = 0, uint64_t param4 = 0, uint64_t param5 = 0)
	restrict (amp) {
		return send(sc | KFD_SC_NONBLOCK_FLAG, param0, param1, param2,
		     param3, param4, param5);
	}
};

int syscalls::init(size_t elements)
{
	void *sc_area = NULL;
	hsaKmtOpenKFD();
	HSAKMT_STATUS s = hsaKmtGetSyscallArea(elements, &sc_area);
	::std::cout << "Requested syscall area: " << s << " address: "
	            << sc_area << ::std::endl;

	if (!sc_area)
		return -1;

	syscalls_ = static_cast<kfd_sc*>(sc_area);
	elements_ = elements;
	return 0;
}

void sendmsg_wrapper__(void)restrict(amp);

int syscalls::send(int sc,
	uint64_t param0, uint64_t param1, uint64_t param2,
	uint64_t param3, uint64_t param4, uint64_t param5)
restrict(amp)
{
	if (syscalls_ == NULL || elements_ == 0)
		return EINVAL;
	int idx = amp_get_global_id(0) % elements_;

	//this should be atomic swap
	if (syscalls_[idx].status != KFD_SC_STATUS_FREE)
		return EAGAIN;
	syscalls_[idx].sc_num = sc;
	syscalls_[idx].status = KFD_SC_STATUS_READY;
	//TODO params
//	if (idx % 64 == 0)
// TODO this need to be implemented in hcc
//		sendmsg_wrapper__();
	return 0;
}

int main(void)
{
	syscalls::get().init(WORKITEMS);

	/* for some reason we need a reference,
	 * function scope globals are probably broken */
	syscalls &local = syscalls::get();

	/* show that we care outside of kernel to prevent DCE */
	int ret;
	parallel_for_each(extent<1>(1), [&](index<1> idx) restrict(amp)
	{
		ret = local.send_nonblock(0);
	});
	hsaKmtCloseKFD();
	// Check
//	for (int i = 0; i < WORKITEMS

	return 0;
}
