#include <iostream>
#include <hsakmt.h>

enum {
	WORKITEMS=32
};

int main(void)
{
	void *sc_area = NULL;
	hsaKmtOpenKFD();
	HSAKMT_STATUS s = hsaKmtGetSyscallArea(WORKITEMS, &sc_area);
	::std::cout << "Requested syscall area: " << s << " address: "
	            << sc_area << ::std::endl;
	return 0;
}
