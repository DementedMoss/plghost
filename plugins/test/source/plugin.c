
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>
#include "plugin.h"


#define THREAD_STACK_SIZE 0x1000

static Thread pluginThread;
static bool unloadRequest;
static PluginContext* ctx;
static const PluginOps* pluginOps;

static void TestPlugin_Main(void* arg)
{
	while(!unloadRequest)
	{
		pluginOps->print(ctx, "plugin thread...\n");
		svcSleepThread(1000000000ULL);
	}
}

static void TestPlugin_Load(PluginContext* context, const PluginOps* ops)
{
	ctx = context;
	pluginOps = ops;
	pluginOps->print(ctx, "loading plugin: test\n");

	fprintf(stdout, "standard IO test\n");

	unloadRequest = false;

	s32 prio;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	pluginThread = threadCreate(TestPlugin_Main, NULL, THREAD_STACK_SIZE, prio, -2, true);
}

static void TestPlugin_Unload(PluginContext* ctx)
{
	pluginOps->print(ctx, "unloading plugin: test\n");
	unloadRequest = true;

	threadJoin(pluginThread, UINT64_MAX);
	threadFree(pluginThread);
	pluginThread = NULL;
}

static void TestPlugin_Tick(PluginContext* ctx)
{
	pluginOps->print(ctx, "plugin tick\n");
}

void * _sbrk_r(struct _reent *ptr, ptrdiff_t incr)
{
	return pluginOps->sbrk(ctx, incr);
}

// not actually used for anything, but an entry point is required
__attribute__((section(".init"))) void _start(void) {}

DECLARE_PLUGIN(test) =
{
		.magic = PLUGIN_MAGIC,
		.version = PLUGIN_VER,
		.name = "test",

		.syscalls = &__syscalls,
		.load = TestPlugin_Load,
		.unload = TestPlugin_Unload,
		.tick = TestPlugin_Tick,
};
