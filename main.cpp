#include <array>
#include <iostream>
#include <hsakmt.h>
#include <linux/kfd_sc.h>

#include <amp.h>
using namespace concurrency;

enum {
	WORKITEMS=64
};

#define ARG_COUNT (sizeof(kfd_sc::arg) / sizeof(kfd_sc::arg[0]))

class syscalls {
	using arg_array = ::std::array<uint64_t, ARG_COUNT>;

	kfd_sc *syscalls_ = NULL;
	size_t elements_ = 0;
public:
	int init(size_t elements);
	static syscalls& get() restrict (amp,cpu)
	{
		static syscalls instance;
		return instance;
	}
	int send(int sc, arg_array args = {})
	restrict(amp);
	int send_nonblock(int sc, arg_array args = {})
	restrict (amp) {
		return send(sc | KFD_SC_NONBLOCK_FLAG, args);
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
		ret = local.send_nonblock(0, {1});
	});
	hsaKmtCloseKFD();
	// Check
//	for (int i = 0; i < WORKITEMS

	return 0;
}
