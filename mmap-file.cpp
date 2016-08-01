#include <hc.hpp>
#include <hc_syscalls.h>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdio>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <unistd.h>
#include "test.h"

static bool print_maps = false;
static size_t size = 4096;
static int fd = -1;

enum {
	MAGICK = 0xdeadbeef,
};

static void help(int argc, char *argv[])
{
	::std::cerr << "\t--maps\tPrint content of /proc/self/maps\n";
	::std::cerr << "\t--size\tamount of space to allocate (Default: 4kB)\n";
	::std::cerr << "\t--file\tUse file instead of a generated one\n";
}

static bool parse(const ::std::string &opt, const ::std::string &arg)
{
	if (opt == "--maps") {
		print_maps = true;
		return true;
	}
	if (opt == "--size") {
		size = ::std::stoi(arg);
		return true;
	}
	if (opt == "--file") {
		fd = open(arg.c_str(), O_RDONLY, 0666);
		return true;
	}
	return false;
}

static void dump4(const char* ch)
{
	::std::cerr << "DUMP: " << ch[0] << ", "
	            << ch[1] << ", " << ch[2] << ", " << ch[3] << "\n";
}

static int run_gpu(const test_params &p, ::std::ostream &O, syscalls &sc,
                   int argc, char *argv[])
{
	// HCC Fails if we set this to void*
	using T = int64_t;
	FILE * tmpf = NULL;
	char name[] = "/tmp/XXXXXXX";

	char text[] = "foo";

	if (fd == -1) {
		mkstemp(name);
		tmpf = fopen(name, "wb+");
		fwrite(text, 1, sizeof(text), tmpf);
		fflush(tmpf);
		fd = fileno(tmpf);
		size = sizeof(text);
	} else {
		read(fd, text, 4);
	}

	// HCC is very bad with globals
	size_t lsize = size;
	uint64_t lfd = fd;

	::std::vector<T> ret(p.parallel);
	::std::vector<uint32_t> rdata(p.parallel);

	auto f = [&](hc::index<1> idx) [[hc]] {
		int i = idx[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to wait here, since
			// blocking operation guarantees
			// available slots
			__attribute__((address_space(1))) char * ptr
				 = (__attribute__((address_space(1))) char *)
					sc.send(SYS_mmap,
					       {0, lsize, PROT_READ,
					        MAP_PRIVATE, lfd, 0});
			ret[i] = (T)ptr;
			if ((int64_t)ptr > 0) {
				union {
					char txt[4];
					uint32_t val;
				} data;
				data.txt[0] = ptr[0];
				data.txt[1] = ptr[1];
				data.txt[2] = ptr[2];
				data.txt[3] = ptr[3];
				rdata[i] = data.val;
			}
		}
	};
	auto f_s = [&](hc::tiled_index<1> tidx) [[hc]] {
		int i = tidx.global[0];
		for (size_t j = 0; j < p.serial; ++j) {
			// we don't need to check for available slot here,
			// since blocking operation guarantees
			// available slots. but we can sync across WGs
			tidx.barrier.wait();
			__attribute__((address_space(1))) char * ptr
				 = (__attribute__((address_space(1))) char *)
					sc.send(SYS_mmap,
					       {0, lsize, PROT_READ,
					        MAP_PRIVATE, lfd, 0});
			ret[i] = (T)ptr;
			if ((int64_t)ptr > 0) {
				union {
					char txt[4];
					uint32_t val;
				} data;
				data.txt[0] = ptr[0];
				data.txt[1] = ptr[1];
				data.txt[2] = ptr[2];
				data.txt[3] = ptr[3];
				rdata[i] = data.val;
			}
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
	O << us.count() << std::endl;

	if (print_maps) {
		static char buffer [4096] = {0};
		::std::ifstream maps("/proc/self/maps");
		while (maps.good()) {
			size_t count = maps.read(buffer, sizeof(buffer)).gcount();
			::std::cout.write(buffer, count);
		}
		maps.close();
	}

	for (const T& r : ret)
	if (::std::any_of(ret.begin(), ret.end(), [&](T ret) {
		return (void*)ret == MAP_FAILED; }))
		::std::cerr << "Some memory allocations failed\n";
	if (tmpf) {
		fclose(tmpf);
		remove(name);
	}

	for (size_t i = 0; i < ret.size(); ++i) {
		int *ptr = (int*)ret[i];
		if (ptr == MAP_FAILED) {
			::std::cerr << "Failed memory allocations\n";
			return 1;
		}
		if (::std::memcmp(text, &rdata[i], 4) != 0) {
			dump4(text);
			dump4((const char*)&rdata[i]);
			::std::cerr << "GPU read data do not match\n";
			return 1;
		}
		if ((ret[i] < 0) || (::std::memcmp(text, ptr, 4) != 0)) {
			dump4(text);
			if (ret[i] > 0)
				dump4((const char*)ptr);
			::std::cerr << "Mapped data do not match\n";
			return 1;
		}
		int rc = munmap(ptr, size);
		if (rc) {
			::std::cerr << "Failed unmap: " << ptr << "(" << (long)ptr << ")\n";
			return 1;
		}
	}
	return 0;
};

static int run_cpu(const test_params &p, ::std::ostream &O,
                   int argc, char *argv[])
{
	// HCC Fails if we set this to void*
	using T = int64_t;
	FILE * tmpf = NULL;
	char name[] = "/tmp/XXXXXXX";

	char text[] = "foo";

	if (fd == -1) {
		mkstemp(name);
		tmpf = fopen(name, "wb+");
		fwrite(text, 1, sizeof(text), tmpf);
		fflush(tmpf);
		fd = fileno(tmpf);
		size = sizeof(text);
	} else {
		read(fd, text, 4);
	}

	// HCC is very bad with globals
	size_t lsize = size;
	uint64_t lfd = fd;

	::std::vector<T> ret(p.parallel);
	::std::vector<uint32_t> rdata(p.parallel);

	auto start = ::std::chrono::high_resolution_clock::now();
	for (size_t i = 0; i < p.parallel; ++i) {
		char * ptr = (char *)mmap(NULL, lsize, PROT_READ,
					        MAP_PRIVATE, lfd, 0);
			ret[i] = (T)ptr;
			if ((int64_t)ptr > 0) {
				union {
					char txt[4];
					uint32_t val;
				} data;
				data.txt[0] = ptr[0];
				data.txt[1] = ptr[1];
				data.txt[2] = ptr[2];
				data.txt[3] = ptr[3];
				rdata[i] = data.val;
			}
	}
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	O << us.count() << std::endl;

	if (print_maps) {
		static char buffer [4096] = {0};
		::std::ifstream maps("/proc/self/maps");
		while (maps.good()) {
			size_t count = maps.read(buffer, sizeof(buffer)).gcount();
			::std::cout.write(buffer, count);
		}
		maps.close();
	}

	for (const T& r : ret)
	if (::std::any_of(ret.begin(), ret.end(), [&](T ret) {
		return (void*)ret == MAP_FAILED; }))
		::std::cerr << "Some memory allocations failed\n";
	if (tmpf) {
		fclose(tmpf);
		remove(name);
	}

	for (size_t i = 0; i < ret.size(); ++i) {
		int *ptr = (int*)ret[i];
		if (ptr == MAP_FAILED) {
			::std::cerr << "Failed memory allocations\n";
			return 1;
		}
		if (::std::memcmp(text, &rdata[i], 4) != 0) {
			dump4(text);
			dump4((const char*)&rdata[i]);
			::std::cerr << "GPU read data do not match\n";
			return 1;
		}
		if ((ret[i] < 0) || (::std::memcmp(text, ptr, 4) != 0)) {
			dump4(text);
			if (ret[i] > 0)
				dump4((const char*)ptr);
			::std::cerr << "Mapped data do not match\n";
			return 1;
		}
		int rc = munmap(ptr, size);
		if (rc) {
			::std::cerr << "Failed unmap: " << ptr << "(" << (long)ptr << ")\n";
			return 1;
		}
	}
	return 0;
};

struct test test_instance = {
	.run_gpu = run_gpu,
	.run_cpu = run_cpu,
	.parse_option = parse,
	.help = help,
	.name = "mmap File",
};
