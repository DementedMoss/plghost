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
	static const plugin_hdr_t __plugin_##name __attribute__((section(".plugin"))) __attribute__((used))


typedef struct
{
	const __syscalls_t* syscalls;
	void (*print)(const char* message);
} plugin_ctx_t;

typedef struct
{
	u32 magic;
	u32 version;
	char name[32];

	void (*load)(const plugin_ctx_t* ctx);
	void (*unload)(const plugin_ctx_t* ctx);
	void (*tick)(const plugin_ctx_t* ctx);
} plugin_hdr_t;



#endif /* PLUGIN_H_ */
