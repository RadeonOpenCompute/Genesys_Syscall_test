#include "test.h"
#include <getopt.h>
#include <iostream>

static const struct option options[] = {
	{"parallel", required_argument, NULL, 'p'},
	{"serial", required_argument, NULL, 's'},
	{"wg-size", required_argument, NULL, 'g'},
	{"nonblock", no_argument, NULL, 'n'},
	{"dont-wait-after", no_argument, NULL, 'd'},
	{"gpu-sync-before", no_argument, NULL, 'y'},
	{"gpu-wait-before", no_argument, NULL, 'w'},
	{"run-on-cpu", no_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
	{"test-help", no_argument, NULL, 't'},
	{NULL, }
};

int main(int argc, char *argv[])
{
	test_params params;
	char c;
	opterr = 0;
	while ((c = getopt_long(argc, argv, "p:s:g:ndywcht", options, NULL)) != -1) {
		switch (c) {
		case 'p': params.parallel = ::std::atoi(optarg); break;
		case 's': params.serial = ::std::atoi(optarg); break;
		case 'g': params.wg_size = ::std::atoi(optarg); break;
		case 'n': params.non_block = true; break;
		case 'd': params.dont_wait_after = true; break;
		case 'y': params.gpu_sync_before = true; break;
		case 'w': params.gpu_wait_before = true; break;
		case 'c': params.cpu = true; break;
		case 't':
			if (test_instance.help)
				test_instance.help(argc, argv);
			else
				::std::cerr << "No test help available!\n";
			return 0;
		default:
			if (test_instance.parse_option &&
			    test_instance.parse_option(argv[optind - 1],
			                               argv[optind] ? argv[optind] : ""))
				break;
			::std::cerr << "Unknown option: " << argv[optind -1]
			            << ::std::endl;
		case 'h':
			::std::cerr << "Available options:\n";
			for (const option &o : options)
				if (o.name)
					::std::cerr << "\t--" << o.name << ", -"
					            << (char)o.val << ::std::endl;
			return 0;
		}
	}
	if (!params.isValid()) {
		::std::cerr << "Invalid configuration: " << params
		            << ::std::endl;
		return 1;
	}
	::std::cout << "Running " << test_instance.name << " " << params
	            << ::std::endl;
	if (params.cpu)
		return test_instance.run_cpu(params, ::std::cout, argc, argv);
	else
		return test_instance.run_gpu(params, ::std::cout, syscalls::get(), argc, argv);
}

int no_cpu(const test_params &params, ::std::ostream &out,
           int argc, char *argv[])
{
	::std::cerr << "No CPU version of the test " << test_instance.name
	            << ::std::endl;
	return 1;
}
