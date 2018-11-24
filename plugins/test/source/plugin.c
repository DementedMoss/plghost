
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>
#include "plugin.h"


#define THREAD_STACK_SIZE 0x1000

static Thread plugin_thread;

static void plugin_main(void* arg)
{
	const plugin_ctx_t* ctx = (const plugin_ctx_t*)arg;

	for(;;)
	{
		ctx->print("plugin thread...\n");
		svcSleepThread(1000000000ULL);
	}
}

static void plugin_load(const plugin_ctx_t* ctx)
{
	__syscalls = *ctx->syscalls;
	ctx->print("loading plugin: test\n");

	void* mem = malloc(32u);

	if(mem == NULL)
	{
		ctx->print("malloc failed\n");
	}

	fprintf(stdout, "hello world\n");
	printf("hello world\n");

	//s32 prio;
	//svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	//plugin_thread = threadCreate(plugin_main, (void*)ctx, THREAD_STACK_SIZE, prio, -2, true);
}

static void plugin_unload(const plugin_ctx_t* ctx)
{
	ctx->print("unloading plugin: test\n");
}

static void plugin_tick(const plugin_ctx_t* ctx)
{
	ctx->print("plugin tick\n");
}

__attribute__((section(".init"))) void _start(const plugin_ctx_t* ctx)
{
}

DECLARE_PLUGIN(test) =
{
		.magic = PLUGIN_MAGIC,
		.version = PLUGIN_VER,
		.name = "test",

		.load = plugin_load,
		.unload = plugin_unload,
		.tick = plugin_tick,
};
