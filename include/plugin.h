/*
 * plugin.h
 */

#ifndef PLUGIN_H_
#define PLUGIN_H_

#include <stdint.h>
#include <sys/iosupport.h>

#define PLUGIN_MAGIC 0x474C504C // LPLG
#define PLUGIN_VER   0

#define DECLARE_PLUGIN(name) \
	static const PluginHeader __plugin_##name __attribute__((section(".plugin"))) __attribute__((used))


typedef struct PluginContext PluginContext;

typedef struct
{
	void*(*sbrk)(PluginContext* plugin, ptrdiff_t incr);
	void (*print)(PluginContext* plugin, const char* message);
} PluginOps;

typedef struct
{
	u32 magic;
	u32 version;
	char name[32];
	__syscalls_t* syscalls;

	void (*load)(PluginContext* ctx, const PluginOps* ops);
	void (*unload)(PluginContext* ctx);
	void (*tick)(PluginContext* ctx);
} PluginHeader;



#endif /* PLUGIN_H_ */
