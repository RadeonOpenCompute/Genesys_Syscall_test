#include <hc.hpp>
#include <hc_syscalls.h>
#include <fstream>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "test.h"

static bool verify = true;

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--no-verify\tVerify that info matches between CPU and GPU (Default: verify)\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--no-verify") {
		verify = false;
		return true;
	}
	return false;
}

unsigned kill_count = 0;

static void handler(int signum, siginfo_t *info, void *)
{
	kill_count += info->si_int;
}

static int sum(int max)
{
	int sum = 0;
	while (max)
		sum += --max;
	return sum;

}

static int run_gpu(const test_params &p, ::std::ostream &O, syscalls &sc,
                   int argc, char *argv[])
{
	// HCC Fails if we set this to void*
	using T = uint64_t;
	::std::vector<T> ret(p.parallel);
	::std::vector<siginfo_t> siginfo(p.parallel);

	for (auto &s : siginfo) {
		s = {};
		s.si_code = SI_QUEUE;
		s.si_pid = getpid();
		s.si_uid = getuid();
	}

	int use_signal = SIGRTMIN;
	struct sigaction sa = {};
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(use_signal, &sa, NULL);

	pid_t pid = getpid();

	auto f = [&](hc::index<1> idx) [[hc]] {
		int i = idx[0];
		sigval val;
		val.sival_int = i;
		siginfo_t * s = &siginfo[i];
		s->si_value = val;
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			ret[i] = sc.send(SYS_rt_sigqueueinfo, {(uint64_t)pid, (uint64_t)use_signal, (uint64_t)s});
		}
	};
	auto f_s = [&](hc::tiled_index<1> tidx) [[hc]] {
		int i = tidx.global[0];
		sigval val;
		val.sival_int = i;
		siginfo_t * s = &siginfo[i];
		s->si_value = val;
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to check for available slot here,
			// since blocking operation guarantees
			// available slots. but we can sync across WGs
			tidx.barrier.wait();
			ret[i] = sc.send(SYS_rt_sigqueueinfo, {(uint64_t)pid, (uint64_t)use_signal, (uint64_t)s});
			tidx.barrier.wait();
		}
	};
	auto f_n = [&](hc::index<1> idx) [[hc]] {
	};
	auto f_w_n = [&](hc::index<1> idx) [[hc]] {
	};
	auto f_s_n = [&](hc::tiled_index<1> tidx) [[hc]] {
	};

	auto start = ::std::chrono::high_resolution_clock::now();
	test_run(p, sc, f, f_s, f_n, f_s_n, f_w_n);
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);

	sigset_t set;
	do {
		sigemptyset(&set);
		sigpending(&set);
	} while (sigismember(&set, use_signal));

	O << us.count() << std::endl;


	if (::std::any_of(ret.begin(), ret.end(), [&](T ret) {
		return (ret != 0); }))
		::std::cerr << "Some signals failed \n";

	if (verify) {
		int expect = sum(p.parallel);
		if (kill_count != expect)
			::std::cerr << "ERROR: signal value sum does not match: "
			            << kill_count << " vs. " << expect << "\n";
	}
	return 0;
};

static int run_cpu(const test_params &p, ::std::ostream &O,
                   int argc, char *argv[])
{
	// HCC Fails if we set this to void*
	using T = uint64_t;
	::std::vector<T> ret(p.parallel);

	struct sigaction sa = {};
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGRTMIN, &sa, NULL);

	int pid = getpid();

	auto start = ::std::chrono::high_resolution_clock::now();
	for (size_t i = 0; i < p.parallel; ++i) {
		for (size_t j = 0; j < p.serial; ++j) {
			sigval val;
			val.sival_int = i;
			ret[i] = sigqueue(pid, SIGRTMIN, val);
		}
	};
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;


	if (::std::any_of(ret.begin(), ret.end(), [&](T ret) {
		return (ret != 0); }))
		::std::cerr << "Some signals failed \n";

	if (verify) {
		int expect = sum(p.parallel);
		if (kill_count != expect)
			::std::cerr << "ERROR: signal value sum does not match: "
			            << kill_count << " vs. " << expect << "\n";
	}
	return 0;
};


struct test test_instance = {
	.run_gpu = run_gpu,
	.run_cpu = run_cpu,
	.parse_option = parse,
	.help = help,
	.name = "sigqueue",
};
