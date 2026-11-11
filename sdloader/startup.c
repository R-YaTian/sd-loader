#include <mem/heap.h>
#include <soc/bpmp.h>
#include <soc/hw_init.h>
#include <utils/types.h>
#include <utils/util.h>
#include <memory_map.h>

#if PICOFLY_TOOLBOX || TARGET_HWFLY
#include <input/joycon.h>
#endif

extern void pivot_stack(u32 stack_top);

void main();

__attribute__((noreturn)) void ipl_main()
{
	hw_init();
#if PICOFLY_TOOLBOX || TARGET_HWFLY
	jc_init_hw();
#endif
	pivot_stack(IPL_STACK_TOP);
	heap_init((void*)IPL_HEAP_START);

	main();

	power_set_state(POWER_OFF);

	while (1)
	{
		bpmp_halt();
	}
}
