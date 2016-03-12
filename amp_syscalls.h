#ifndef AMP_SYSCALLS_H
#define AMP_SYSCALLS_H

#include <array>
#include <linux/kfd_sc.h>

#define ARG_COUNT (sizeof(kfd_sc::arg) / sizeof(kfd_sc::arg[0]))

class syscalls {
	using arg_array = ::std::array<uint64_t, ARG_COUNT>;

	kfd_sc *syscalls_ = NULL;
	size_t elements_ = 0;
	syscalls(const syscalls&) = delete;
	syscalls() {};
	~syscalls();
public:
	int init(size_t elements);
	static syscalls& get() restrict (amp,cpu);
	int send(int sc, arg_array args = {})
	restrict(amp);
	int send_nonblock(int sc, arg_array args = {})
	restrict (amp) {
		return send(sc | KFD_SC_NONBLOCK_FLAG, args);
	}
};
#endif
