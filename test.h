#ifndef TEST_H
#define TEST_H

#ifdef __HCC__
#include <hc_syscalls.h>
#include <hc.hpp>
#endif

#include <cstddef>
#include <ostream>
#include <vector>

struct test_params {
	size_t parallel = 1;
	size_t serial = 1;
	size_t wg_size = 1;
	bool non_block = false;
	bool dont_wait_after = false;
	bool gpu_sync_before = false;
	bool gpu_wait_before = false;
	bool cpu = false;

	bool isValid() const
	{
		return (!gpu_sync_before ||  !gpu_wait_before) && parallel > 0
			&& (parallel % wg_size == 0)
			&& (wg_size <= 1024) && (wg_size > 0);
	}
	bool fitsGPU() const
	{
		size_t waves_per_wg = wg_size / ::std::min<size_t>(64, wg_size);
		size_t waves = (parallel / wg_size) * waves_per_wg;
		return waves <= 320; // Kaveri/Carrizo
	}
};

static inline ::std::ostream & operator << (::std::ostream &O, const test_params &t)
#ifdef __HCC__
[[cpu]]
#endif
{
	O << "(parallel: " << t.parallel
	  << ", serial: " << t.serial
	  << ", wg_size: " << t.wg_size
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

#ifndef __HCC__
struct syscalls {
	static syscalls& get() {
		static syscalls instance;
		return instance;
	}
};
#endif

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

#ifdef __HCC__
template<class F, class FS, class FN, class FNS, class FNW>
void test_run(const test_params &p, const syscalls &sc,
              F& f, FS &fs, FN &fn, FNS &fns, FNW &fnw)
{
	auto extent = hc::extent<1>::extent(p.parallel);
	if (!p.non_block) {
		if (p.gpu_sync_before)
			parallel_for_each(extent.tile(p.wg_size), fs).wait();
		else
			parallel_for_each(extent.tile(p.wg_size), f).wait();
	} else {
		if (p.gpu_sync_before)
			parallel_for_each(extent.tile(p.wg_size), fns).wait();
		else if (p.gpu_wait_before)
			parallel_for_each(extent.tile(p.wg_size), fnw).wait();
		else
			parallel_for_each(extent.tile(p.wg_size), fn).wait();
	}
	if (p.non_block && !p.dont_wait_after)
		sc.wait_all();
}
#endif

FILE * init_tmp_file(const ::std::vector<char> &data, size_t count, char* name);

#endif
