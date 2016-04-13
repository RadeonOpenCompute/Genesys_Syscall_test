#include <amp.h>
#include <iostream>
#include <hsakmt.h>

#include "amp_syscalls.h"


syscalls& syscalls::get() restrict (amp,cpu)
{
	static syscalls instance;
	return instance;
}
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

kfd_sc &syscalls::get_slot() restrict(amp)
{
	int idx = amp_get_global_id(0) % elements_;
	return syscalls_[idx];
}

syscalls::status_t &syscalls::get_atomic_status(kfd_sc &slot) restrict (amp,cpu)
{
	//this is ugly and probably broken
	status_t &status = *(status_t*)&slot.status;
	return status;
}

int syscalls::wait_get_ret() restrict(amp)
{
	kfd_sc &slot = get_slot();
	status_t &status = get_atomic_status(slot);
	while (status != KFD_SC_STATUS_FINISHED) ;
	int ret = slot.arg[0];
	status = KFD_SC_STATUS_FREE;
	return ret;
}

int syscalls::send_common(int sc, arg_array args) restrict(amp)
{
	if (syscalls_ == NULL || elements_ == 0)
		return EINVAL;

	kfd_sc &slot = get_slot();
	status_t &status = get_atomic_status(slot);

	uint32_t expected = KFD_SC_STATUS_FREE;
	if (!status.compare_exchange_strong(expected, KFD_SC_STATUS_GPU))
		return EAGAIN;
	slot.sc_num = sc;
	// std::copy should work here if it were implemented for AMP
	for (int i = 0; i < args.size(); ++i)
		slot.arg[i] = args[i];

	// This is necessary, atomic status op does not work as barrier
	__hsail_barrier();
	status = KFD_SC_STATUS_READY;
	// These are scalar, so they get executed only once per wave.
	__hsa_sendmsg(0);
	return 0;
}

syscalls::~syscalls()
{
	hsaKmtCloseKFD();
}
