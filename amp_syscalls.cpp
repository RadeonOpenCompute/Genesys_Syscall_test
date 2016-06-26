#include <amp.h>
#include <iostream>
#include <hsakmt.h>

#include "amp_syscalls.h"


syscalls& syscalls::get() restrict (amp,cpu)
{
	static syscalls instance;
	return instance;
}

syscalls::syscalls()
{
	void *sc_area = NULL;
	HSAuint32 elements = 0;
	hsaKmtOpenKFD();
	HSAKMT_STATUS s = hsaKmtGetSyscallArea(&elements, &sc_area);
	if (sc_area) {
		syscalls_ = static_cast<kfd_sc*>(sc_area);
		elements_ = elements;
	}
}

syscalls::~syscalls()
{
	if (syscalls_)
		hsaKmtFreeSyscallArea();
	hsaKmtCloseKFD();
}

extern "C" void __hsa_sendmsg(uint32_t msg)restrict(amp);
//extern "C" void __hsa_sendmsghalt(void)restrict(amp);
//Halt version is not ready yet
extern "C" void __hsa_fence(void)restrict(amp);
extern "C" void __hsail_barrier(void)restrict(amp);

extern "C" uint32_t __hsail_get_lane_id(void)restrict(amp);
extern "C" uint32_t __hsa_gethwid(void)restrict(amp);

kfd_sc &syscalls::get_slot() restrict(amp)
{
	uint32_t id = __hsa_gethwid();
	unsigned slot =
		(((id >> 4) & 0x3) | ((id >> 6) & 0x1fc)) * 10 + (id & 0xf);
	int idx = (slot * 64) + __hsail_get_lane_id();
	return syscalls_[idx % elements_];
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

void syscalls::wait_all() restrict(amp)
{
	kfd_sc &slot = get_slot();
	status_t &status = get_atomic_status(slot);
	while (status != KFD_SC_STATUS_FINISHED &&
	       status != KFD_SC_STATUS_FREE);
	//TODO we can probably use s_sleep here
	__hsail_barrier();
}

void syscalls::wait_all() restrict(cpu)
{
	for (unsigned i = 0; i < elements_; ++i) {
		status_t &status = get_atomic_status(syscalls_[i]);
		while (status != KFD_SC_STATUS_FINISHED &&
		       status != KFD_SC_STATUS_FREE)
			::std::this_thread::yield();
	} 
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

	// This fence might be unnecessary, MUBUF intructions complete
	// in-order. However, FLAT instructions do not. It might be the
	// case that the ordering is maintained in this case. However,
	// the specs explicitly say:
	// "the only sensible S_WAITCNT value to use after FLAT instructions
	// is zero" (CI ISA p. 85)
	__hsa_fence();
	status = KFD_SC_STATUS_READY;
	// Make sure the status update is visible before issuing interrupt
	// These are scalar, so they get executed only once per wave.
	__hsa_fence();
	__hsa_sendmsg(0);
	return 0;
}
