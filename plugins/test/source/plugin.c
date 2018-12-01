
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>
#include "plugin.h"


#define THREAD_STACK_SIZE 0x1000

static Thread plugin_thread;
static plugin_ctx_t* ctx;
static const plugin_ops_t* plugin_ops;

static void plugin_main(void* arg)
{
	plugin_ops->print(ctx, "plugin thread...\n");
	threadExit(0);
}

static void plugin_load(plugin_ctx_t* context, const plugin_ops_t* ops)
{
	ctx = context;
	plugin_ops = ops;
	plugin_ops->print(ctx, "loading plugin: test\n");

	void* mem = malloc(32u);

	if(mem == NULL)
	{
		plugin_ops->print(ctx, "malloc failed\n");
	}

	fprintf(stdout, "hello world\n");
	printf("hello world\n");

	s32 prio;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	plugin_thread = threadCreate(plugin_main, NULL, THREAD_STACK_SIZE, prio, -2, true);
}

static void plugin_unload(plugin_ctx_t* ctx)
{
	plugin_ops->print(ctx, "unloading plugin: test\n");
}

static void plugin_tick(plugin_ctx_t* ctx)
{
	plugin_ops->print(ctx, "plugin tick\n");
}

void * _sbrk_r(struct _reent *ptr, ptrdiff_t incr)
{
	return plugin_ops->sbrk(ctx, incr);
}

__attribute__((section(".init"))) void _start(const plugin_ops_t* ctx)
{

}

DECLARE_PLUGIN(test) =
{
		.magic = PLUGIN_MAGIC,
		.version = PLUGIN_VER,
		.name = "test",

		.syscalls = &__syscalls,
		.load = plugin_load,
		.unload = plugin_unload,
		.tick = plugin_tick,
};
