
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <limits.h>

#include <3ds.h>

#include "plugin.h"
#include "3dsx.h"

#define RELOCBUFSIZE 512
#define PLUGIN_DIR "/plghost/plugins"

typedef struct
{
	void* buffer;
	size_t size;
	MemPerm perm;
} plugin_segment_t;

typedef struct plugin_ctx
{
	char file[32];
	plugin_hdr_t* hdr;
	plugin_segment_t segments[3];
	void* brk;
	struct plugin_ctx* next;
} plugin_t;


static _3DSX_Reloc reloc_tbl[RELOCBUFSIZE];

static bool Plugin_FileRead(Handle file, u64* pos, void* buffer, u32 size)
{
	u32 bytes_read;

	if(R_SUCCEEDED(FSFILE_Read(file, &bytes_read, *pos, buffer, size)))
	{
		*pos += bytes_read;
		return bytes_read == size;
	}

	return false;
}

static bool Plugin_LoadFrom3dsx(Handle file, plugin_t* plugin)
{
	u32 i, j, k, m;
	u64 off = 0u;

	// read the header
	_3DSX_Header hdr;
	if(!Plugin_FileRead(file, &off, &hdr, sizeof(hdr)))
	{
		return false;
	}

	if (hdr.magic != _3DSX_MAGIC)
	{
		return false;
	}

	plugin->segments[0].size = (hdr.codeSegSize+0xFFF) &~ 0xFFF;
	plugin->segments[1].size = (hdr.rodataSegSize+0xFFF) &~ 0xFFF;
	plugin->segments[2].size = ((hdr.dataSegSize+0xFFF) &~ 0xFFF) + 0x4000u;

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
		if(!Plugin_FileRead(file, &off, &relocs[i*nRelocTables], nRelocTables*4))
		{
			goto failed;
		}
	}

	// Read the code segment
	if(hdr.codeSegSize != 0u && plugin->segments[0].buffer != NULL)
	{
		if (!Plugin_FileRead(file, &off, plugin->segments[0].buffer, hdr.codeSegSize))
		{
			goto failed;
		}
	}

	// Read the rodata segment
	if(hdr.rodataSegSize != 0u && plugin->segments[1].buffer != NULL)
	{
		if (!Plugin_FileRead(file, &off, plugin->segments[1].buffer, hdr.rodataSegSize))
		{
			goto failed;
		}
	}

	// Read the data segment
	u32 data_size = hdr.dataSegSize - hdr.bssSize;
	if(data_size != 0u && plugin->segments[2].buffer != NULL)
	{
		if (!Plugin_FileRead(file, &off, plugin->segments[2].buffer, data_size))
		{
			goto failed;
		}

		plugin->brk = (uint8_t*)plugin->segments[2].buffer + ((hdr.dataSegSize+0xFFF) &~ 0xFFF);
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

				if (!Plugin_FileRead(file, &off, reloc_tbl, toDo*sizeof(_3DSX_Reloc)))
				{
					goto failed;
				}

				for (k = 0; k < toDo && pos < endPos; k ++)
				{
					pos += reloc_tbl[k].skip;
					u32 num_patches = reloc_tbl[k].patch;
					for (m = 0; m < num_patches && pos < endPos; m ++)
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

static void* Plugin_sbrk(plugin_ctx_t* plugin, ptrdiff_t incr)
{
	printf("%s: sbrk (incr=%u)\n", plugin->hdr->name, incr);

	if(incr != 0)
	{
		ptrdiff_t size = (ptrdiff_t)plugin->brk - (ptrdiff_t)plugin->segments[2].buffer;
		size += incr;

		if(size < 0 || size > plugin->segments[2].size)
		{
			return (void*)-1;
		}

		plugin->brk = (uint8_t*)plugin->brk + incr;
	}

	return plugin->brk;
}

static void Plugin_print(plugin_ctx_t* plugin, const char* msg)
{
	printf("%s", msg);
}

static const plugin_ops_t plugin_ops =
{
		.sbrk = Plugin_sbrk,
		.print = Plugin_print
};

static bool Plugin_LoadFromFile(plugin_t* plugin, const char* path)
{
	u16 plugin_path_u16[PATH_MAX+1];
	Handle file_handle;
	bool result = false;

	// convert file path to utf16
	ssize_t len = utf8_to_utf16(plugin_path_u16, (u8*)path, PATH_MAX);
	if(len > PATH_MAX)
	{
		return false;
	}
	plugin_path_u16[len] = 0u;

	// open the file
	if(R_SUCCEEDED(FSUSER_OpenFileDirectly(&file_handle, ARCHIVE_SDMC,
			fsMakePath(PATH_ASCII, ""), fsMakePath(PATH_UTF16, plugin_path_u16), FS_OPEN_READ, 0)))
	{
		result = Plugin_LoadFrom3dsx(file_handle, plugin);
		FSFILE_Close(file_handle);
	}

	return result;
}

static Result Plugin_ApplyPermissions(plugin_t** list)
{
	Result res;
	Handle process_handle;
	u32 process_id;

	res = svcGetProcessId(&process_id, CUR_PROCESS_HANDLE);
	if(!R_SUCCEEDED(res)) { return res; }
	printf("process id: %lu\n", process_id);

	res = svcOpenProcess(&process_handle, process_id);
	if(!R_SUCCEEDED(res)) { return res; }
	printf("process open\n");

	plugin_t* plugin = *list;
	while(plugin != NULL)
	{
		for(int i = 0; i < 3; ++i)
		{
			if(plugin->segments[i].buffer == NULL)
			{
				continue;
			}

			printf("changing permissions addr=0x%lX, size=0x%X\n", (u32)plugin->segments[i].buffer, plugin->segments[i].size);
			res = svcControlProcessMemory(process_handle, (u32)plugin->segments[i].buffer, (u32)plugin->segments[i].buffer,
					plugin->segments[i].size, MEMOP_PROT, plugin->segments[i].perm);
			if(!R_SUCCEEDED(res))
			{
				fprintf(stderr, "svcControlProcesMemory failed (err=%ld)\n", res);
			}
		}

		plugin = plugin->next;
	}

	svcCloseHandle(process_handle);

	return res;
}

static Result Plugin_LoadPlugins(plugin_t** list)
{
	Result res;
	Handle dir_handle;
	FS_Archive sdmc_archive;

	res = FSUSER_OpenArchive(&sdmc_archive, ARCHIVE_SDMC, fsMakePath(PATH_ASCII, ""));
	if(!R_SUCCEEDED(res)){ return res; }

	res = FSUSER_OpenDirectory(&dir_handle, sdmc_archive, fsMakePath(PATH_ASCII, PLUGIN_DIR));
	if(!R_SUCCEEDED(res)) { return res; }

	u32 nread = 0u;
	do
	{
		FS_DirectoryEntry entry;

		res = FSDIR_Read(dir_handle, &nread, 1u, &entry);
		if(!R_SUCCEEDED(res)) { return res; }

		if(nread != 0u &&
				(entry.attributes & FS_ATTRIBUTE_DIRECTORY) == 0u &&
				entry.fileSize != 0u)
		{
			plugin_t* plugin;
			char plugin_path[PATH_MAX+1];
			char plugin_name[128];

			// convert the file name to utf-8
			ssize_t len = utf16_to_utf8((u8*)plugin_name, entry.name, sizeof(plugin_name));
			if(len >= sizeof(plugin_name))
			{
				continue;
			}
			plugin_name[len] = '\0';

			// make sure the extension is 3dsx
			char* ext = strrchr(plugin_name, '.');
			if(ext == NULL || strcasecmp(ext, ".3dsx") != 0)
			{
				continue;
			}

			plugin = (plugin_t*)calloc(1u, sizeof(*plugin));
			if(plugin == NULL)
			{
				break;
			}

			// generate the full path to file
			snprintf(plugin_path, sizeof(plugin_path), PLUGIN_DIR "/%s", plugin_name);

			if(Plugin_LoadFromFile(plugin, plugin_path))
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

	res = FSDIR_Close(dir_handle);
	if(!R_SUCCEEDED(res)) { return res; }

	res = FSUSER_CloseArchive(sdmc_archive);
	if(!R_SUCCEEDED(res)) { return res; }

	Plugin_ApplyPermissions(list);

	for(plugin_t* plugin = *list; plugin != NULL; plugin = plugin->next)
	{
		printf("\t%s\n", plugin->hdr->name);

		*plugin->hdr->syscalls = __syscalls;
		plugin->hdr->load(plugin, &plugin_ops);
	}

	return 0;
}

int main(int argc, char** argv)
{
	plugin_t* plugins = NULL;

	gfxInitDefault();
	fsInit();

	consoleInit(GFX_TOP, NULL);
	printf("Starting plghost...\n");

	printf("loading plugins...\n");
	Plugin_LoadPlugins(&plugins);


	while(aptMainLoop())
	{
		//exit when user hits B
		hidScanInput();
		if(keysHeld()&KEY_B)break;

		for(plugin_t* plugin = plugins; plugin != NULL; plugin = plugin->next)
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
