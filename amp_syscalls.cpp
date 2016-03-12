#include <amp.h>
#include <iostream>
#include <hsakmt.h>

#include "amp_syscalls.h"

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

extern "C" void __hsa_sendmsg(uint32_t msg)restrict(amp);
//extern "C" void __hsa_sendmsghalt(void)restrict(amp);
//Halt version is not ready yet
extern "C" void __hsail_barrier(void)restrict(amp);

int syscalls::send(int sc, arg_array args)
restrict(amp)
{
	if (syscalls_ == NULL || elements_ == 0)
		return EINVAL;
	int idx = amp_get_global_id(0) % elements_;
	//this should be atomic swap
	if (syscalls_[idx].status != KFD_SC_STATUS_FREE)
		return EAGAIN;
	kfd_sc &slot = syscalls_[idx];
	slot.sc_num = sc;
	slot.status = KFD_SC_STATUS_READY;
	// std::copy should work here if it were implemented for AMP
	for (int i = 0; i < args.size(); ++i)
		slot.arg[i] = args[i];

	// These are scalar, so they get executed only once per wave.
	__hsail_barrier();
	__hsa_sendmsg(0);
	return 0;
}

syscalls::~syscalls()
{
	hsaKmtCloseKFD();
}
