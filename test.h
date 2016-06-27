#ifndef TEST_H
#define TEST_H

#include "amp_syscalls.h"
#include <cstddef>
#include <ostream>

struct test_params {
	size_t parallel = 1;
	size_t serial = 1;
	bool non_block = false;
	bool dont_wait_after = false;
	bool gpu_sync_before = false;
	bool gpu_wait_before = false;
	bool cpu = false;

	bool isValid() const
	{
		return (!gpu_sync_before ||  !gpu_wait_before) && parallel > 0;
	}
};

static inline ::std::ostream & operator << (::std::ostream &O, const test_params &t) restrict(cpu)
{
	O << "(parallel: " << t.parallel
	  << ", serial: " << t.serial
	  << ", non_block: " << t.non_block
	  << ", dont_wait_after: " << t.dont_wait_after
	  << ", gpu_sync_before: " << t.gpu_sync_before
	  << ", gpu_wait_before: " << t.gpu_wait_before
	  << ", cpu: " << t.cpu
	  << ")";
	return O;
}

extern int no_cpu(const test_params &params, ::std::ostream &out,
                  int argc, char *argv[]);

struct test {
	int (*run_gpu)(const test_params &params, ::std::ostream &out,
	           syscalls &sc, int argc, char *argv[]);
	int (*run_cpu)(const test_params &params, ::std::ostream &out,
	           int argc, char *argv[]);
	void (*help)(int argc, char *argv[]);
	bool (*parse_option)(const ::std::string &opt, const ::std::string &arg);
	const char *name;
};

extern struct test test_instance;

#endif
