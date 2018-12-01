
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <limits.h>

#include <3ds.h>

#include "plugin.h"
#include "3dsx.h"

#define PLUGIN_HEAP_SIZE 0x8000
#define RELOCBUFSIZE 512
#define PLUGIN_DIR "/plghost/plugins"

typedef struct
{
	void* buffer;
	size_t size;
	MemPerm perm;
} PluginSegment;

typedef struct PluginContext
{
	char file[32];
	PluginHeader* hdr;
	PluginSegment segments[3];
	void* brk;
	struct PluginContext* next;
} PluginContext;


static _3DSX_Reloc relocTable[RELOCBUFSIZE];

static bool PluginHost_FileRead(Handle file, u64* pos, void* buffer, u32 size)
{
	u32 bytesRead;

	if(R_SUCCEEDED(FSFILE_Read(file, &bytesRead, *pos, buffer, size)))
	{
		*pos += bytesRead;
		return bytesRead == size;
	}

	return false;
}

static bool PluginHost_LoadFrom3dsx(Handle file, PluginContext* plugin)
{
	u32 i, j, k, m;
	u64 off = 0u;

	// read the header
	_3DSX_Header hdr;
	if(!PluginHost_FileRead(file, &off, &hdr, sizeof(hdr)))
	{
		return false;
	}

	if (hdr.magic != _3DSX_MAGIC)
	{
		return false;
	}

	plugin->segments[0].size = (hdr.codeSegSize+0xFFF) &~ 0xFFF;
	plugin->segments[1].size = (hdr.rodataSegSize+0xFFF) &~ 0xFFF;
	plugin->segments[2].size = ((hdr.dataSegSize+0xFFF) &~ 0xFFF) + PLUGIN_HEAP_SIZE;

	plugin->segments[0].perm = MEMPERM_READ | MEMPERM_EXECUTE;
	plugin->segments[1].perm = MEMPERM_READ;
	plugin->segments[2].perm = MEMPERM_READ | MEMPERM_WRITE;

	u32 offsets[2] = { plugin->segments[0].size, plugin->segments[0].size + plugin->segments[1].size };
	u32 nRelocTables = hdr.relocHdrSize/4;

	u32* relocs = malloc(4*3*nRelocTables);
	if(relocs == NULL)
	{
		return false;
	}

	// Allocate memory for the segments
	for(i = 0u; i < 3u; ++i)
	{
		if(plugin->segments[i].size != 0u)
		{
			plugin->segments[i].buffer = aligned_alloc(0x1000u, plugin->segments[i].size);

			if(plugin->segments[i].buffer == NULL)
			{
				goto failed;
			}
		}
	}

	// Skip header for future compatibility.
	off = hdr.headerSize;

	// Read the relocation headers
	for (i = 0; i < 3; i ++)
	{
		if(!PluginHost_FileRead(file, &off, &relocs[i*nRelocTables], nRelocTables*4))
		{
			goto failed;
		}
	}

	// Read the code segment
	if(hdr.codeSegSize != 0u && plugin->segments[0].buffer != NULL)
	{
		if (!PluginHost_FileRead(file, &off, plugin->segments[0].buffer, hdr.codeSegSize))
		{
			goto failed;
		}
	}

	// Read the rodata segment
	if(hdr.rodataSegSize != 0u && plugin->segments[1].buffer != NULL)
	{
		if (!PluginHost_FileRead(file, &off, plugin->segments[1].buffer, hdr.rodataSegSize))
		{
			goto failed;
		}
	}

	// Read the data segment
	u32 dataSize = hdr.dataSegSize - hdr.bssSize;
	if(dataSize != 0u && plugin->segments[2].buffer != NULL)
	{
		if (!PluginHost_FileRead(file, &off, plugin->segments[2].buffer, dataSize))
		{
			goto failed;
		}

		plugin->brk = (void*)((ptrdiff_t)plugin->segments[2].buffer + ((hdr.dataSegSize+0xFFF) &~ 0xFFF));
	}

	// Clear the bss
	memset((char*)plugin->segments[2].buffer + hdr.dataSegSize - hdr.bssSize, 0, hdr.bssSize);

	// Relocate the segments
	for (i = 0; i < 3; i ++)
	{
		for (j = 0; j < nRelocTables; j ++)
		{
			u32 nRelocs = relocs[i*nRelocTables+j];
			if (j >= 2)
			{
				// We are not using this table - ignore it
				off += nRelocs * sizeof(_3DSX_Reloc);
				continue;
			}

			u32* pos = (u32*)plugin->segments[i].buffer;
			u32* endPos = pos + (plugin->segments[i].size/4);

			while (nRelocs)
			{
				u32 toDo = nRelocs > RELOCBUFSIZE ? RELOCBUFSIZE : nRelocs;
				nRelocs -= toDo;

				if (!PluginHost_FileRead(file, &off, relocTable, toDo*sizeof(_3DSX_Reloc)))
				{
					goto failed;
				}

				for (k = 0; k < toDo && pos < endPos; k ++)
				{
					pos += relocTable[k].skip;
					u32 numPatches = relocTable[k].patch;
					for (m = 0; m < numPatches && pos < endPos; m ++)
					{
						u32 inAddr = ((u32)plugin->segments[0].buffer) + ((char*)pos-(char*)plugin->segments[0].buffer);
						u32 origData = *pos;
						u32 subType = origData >> (32-4);
						u32 addr = origData &~ 0xF0000000;

						if (addr < offsets[0])
						{
							addr += (u32)plugin->segments[0].buffer;
						}
						else if (addr < offsets[1])
						{
							addr += (u32)plugin->segments[1].buffer - offsets[0];
						}
						else
						{
							addr += (u32)plugin->segments[2].buffer - offsets[1];
						}

						switch (j)
						{
							case 0:
							{
								if (subType != 0)
								{
									goto failed;
								}
								*pos = addr;
								break;
							}
							case 1:
							{
								u32 data = addr - inAddr;
								switch (subType)
								{
									case 0: *pos = data;            break; // 32-bit signed offset
									case 1: *pos = data &~ BIT(31); break; // 31-bit signed offset
									default:
										goto failed;
								}
								break;
							}
						}
						pos++;
					}
				}
			}
		}
	}

	// Validate the plugin header
	plugin->hdr = plugin->segments[1].buffer;
	if(plugin->hdr->magic != PLUGIN_MAGIC ||
			plugin->hdr->version != PLUGIN_VER)
	{
		goto failed;
	}

	free(relocs);
	return true;

failed:
	if(relocs != NULL)
	{
		free(relocs);
	}

	for (i = 0; i < 3; i ++)
	{
		if(plugin->segments[i].buffer != NULL)
		{
			free(plugin->segments[i].buffer);
			plugin->segments[i].buffer = NULL;
		}
	}

	return false;
}

static void* PluginHost_Sbrk(PluginContext* plugin, ptrdiff_t incr)
{
	ptrdiff_t size;
	printf("%s: sbrk (incr=%u)\n", plugin->hdr->name, incr);

	size = (ptrdiff_t)plugin->brk - (ptrdiff_t)plugin->segments[2].buffer;
	size += incr;
	size = (size+0xFFF) &~ 0xFFF;

	if(size < 0 || size > plugin->segments[2].size)
	{
		return (void*)-1;
	}

	plugin->brk = (void*)((ptrdiff_t)plugin->segments[2].buffer + size);
	return plugin->brk;
}

static void PluginHost_Print(PluginContext* plugin, const char* msg)
{
	printf("%s", msg);
}

static const PluginOps pluginOps =
{
		.sbrk = PluginHost_Sbrk,
		.print = PluginHost_Print
};

static bool PluginHost_LoadFromFile(PluginContext* plugin, const char* path)
{
	u16 pluginPathU16[PATH_MAX+1];
	Handle fileHandle;
	bool result = false;

	// convert file path to utf16
	ssize_t len = utf8_to_utf16(pluginPathU16, (u8*)path, PATH_MAX);
	if(len > PATH_MAX)
	{
		return false;
	}
	pluginPathU16[len] = 0u;

	// open the file
	if(R_SUCCEEDED(FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SDMC,
			fsMakePath(PATH_ASCII, ""), fsMakePath(PATH_UTF16, pluginPathU16), FS_OPEN_READ, 0)))
	{
		result = PluginHost_LoadFrom3dsx(fileHandle, plugin);
		FSFILE_Close(fileHandle);
	}

	return result;
}

static Result PluginHost_ApplyPermissions(PluginContext** list)
{
	Result res;
	Handle processHandle;
	u32 processId;

	res = svcGetProcessId(&processId, CUR_PROCESS_HANDLE);
	if(!R_SUCCEEDED(res)) { return res; }
	printf("process id: %lu\n", processId);

	res = svcOpenProcess(&processHandle, processId);
	if(!R_SUCCEEDED(res)) { return res; }
	printf("process open\n");

	PluginContext* plugin = *list;
	while(plugin != NULL)
	{
		for(int i = 0; i < 3; ++i)
		{
			if(plugin->segments[i].buffer == NULL)
			{
				continue;
			}

			printf("changing permissions addr=0x%lX, size=0x%X\n", (u32)plugin->segments[i].buffer, plugin->segments[i].size);
			res = svcControlProcessMemory(processHandle, (u32)plugin->segments[i].buffer, (u32)plugin->segments[i].buffer,
					plugin->segments[i].size, MEMOP_PROT, plugin->segments[i].perm);
			if(!R_SUCCEEDED(res))
			{
				fprintf(stderr, "svcControlProcesMemory failed (err=%ld)\n", res);
			}
		}

		plugin = plugin->next;
	}

	svcCloseHandle(processHandle);

	return res;
}

static Result PluginHost_LoadPlugins(PluginContext** list)
{
	Result res;
	Handle dirHandle;
	FS_Archive sdmcArchive;

	res = FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_ASCII, ""));
	if(!R_SUCCEEDED(res)){ return res; }

	res = FSUSER_OpenDirectory(&dirHandle, sdmcArchive, fsMakePath(PATH_ASCII, PLUGIN_DIR));
	if(!R_SUCCEEDED(res)) { return res; }

	u32 nread = 0u;
	do
	{
		FS_DirectoryEntry entry;

		res = FSDIR_Read(dirHandle, &nread, 1u, &entry);
		if(!R_SUCCEEDED(res)) { return res; }

		if(nread != 0u &&
				(entry.attributes & FS_ATTRIBUTE_DIRECTORY) == 0u &&
				entry.fileSize != 0u)
		{
			PluginContext* plugin;
			char pluginPath[PATH_MAX+1];
			char pluginName[128];

			// convert the file name to utf-8
			ssize_t len = utf16_to_utf8((u8*)pluginName, entry.name, sizeof(pluginName));
			if(len >= sizeof(pluginName))
			{
				continue;
			}
			pluginName[len] = '\0';

			// make sure the extension is 3dsx
			char* ext = strrchr(pluginName, '.');
			if(ext == NULL || strcasecmp(ext, ".3dsx") != 0)
			{
				continue;
			}

			plugin = (PluginContext*)calloc(1u, sizeof(*plugin));
			if(plugin == NULL)
			{
				break;
			}

			// generate the full path to file
			snprintf(pluginPath, sizeof(pluginPath), PLUGIN_DIR "/%s", pluginName);

			if(PluginHost_LoadFromFile(plugin, pluginPath))
			{
				plugin->next = *list;
				*list = plugin;
			}
			else
			{
				free(plugin);
				plugin = NULL;
			}

		}

	}
	while(nread != 0u && res == 0);

	res = FSDIR_Close(dirHandle);
	if(!R_SUCCEEDED(res)) { return res; }

	res = FSUSER_CloseArchive(sdmcArchive);
	if(!R_SUCCEEDED(res)) { return res; }

	PluginHost_ApplyPermissions(list);

	for(PluginContext* plugin = *list; plugin != NULL; plugin = plugin->next)
	{
		printf("\t%s\n", plugin->hdr->name);

		*plugin->hdr->syscalls = __syscalls;
		plugin->hdr->load(plugin, &pluginOps);
	}

	return 0;
}

int main(int argc, char** argv)
{
	PluginContext* plugins = NULL;

	gfxInitDefault();
	fsInit();

	consoleInit(GFX_TOP, NULL);
	printf("Starting plghost...\n");

	printf("loading plugins...\n");
	PluginHost_LoadPlugins(&plugins);


	while(aptMainLoop())
	{
		//exit when user hits B
		hidScanInput();
		if(keysHeld()&KEY_B)break;

		for(PluginContext* plugin = plugins; plugin != NULL; plugin = plugin->next)
		{
			if(plugin->hdr->tick != NULL)
			{
				plugin->hdr->tick(plugin);
			}
		}

		svcSleepThread(1000000000LL);
	}

	gfxExit();
	fsExit();
	return 0;
}
