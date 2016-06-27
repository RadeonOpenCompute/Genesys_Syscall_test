#ifndef AMP_SYSCALLS_H
#define AMP_SYSCALLS_H

#include <array>
#include <linux/kfd_sc.h>

#define ARG_COUNT (sizeof(kfd_sc::arg) / sizeof(kfd_sc::arg[0]))

class syscalls {
	using arg_array = ::std::array<uint64_t, ARG_COUNT>;
	using status_t  = ::std::atomic_uint;

	kfd_sc *syscalls_ = NULL;
	size_t elements_ = 0;

	syscalls(const syscalls&) = delete;
	syscalls();
	~syscalls();

	kfd_sc &get_slot() restrict(amp);
	status_t &get_atomic_status(kfd_sc &slot) restrict (amp,cpu);
	int send_common(int sc, arg_array args) restrict(amp);
public:
	static syscalls& get() restrict (amp,cpu);
	int wait_get_ret() restrict(amp);
	void wait_all() restrict(cpu);
	void wait_all() restrict(amp);
	void wait_one_free() restrict(amp);
	// TODO: thre should be an AMP way to do this
	static void wg_barrier(void) restrict(amp);
	int send(int sc, arg_array args = {}) restrict (amp)
	{
		int ret = send_common(sc, args);
		if (!ret)
			ret = wait_get_ret();
		return ret;
	}
	int send_nonblock(int sc, arg_array args = {}) restrict (amp)
	{
		return send_common(sc | KFD_SC_NORET_FLAG, args);
	}
};
#endif
