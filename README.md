# plghost - 3DS plugin system
plghost is a project that demonstrates a dynamic plugin system on the Nintendo 3DS.

## Introduction
There is not currently a way to easily run homebrew applications in the background on the 3DS. It is possible to create homebrew sysmodules that can run in the background, but they must be packaged and installed as a cia and then manually launched from another applicaton. This project attempts to address this problem by allowing homebrew plugins to be automatically loaded from the sdcard to run in the background while other games, apps, etc. are running.

## Plugin format
Plugins are compiled into the standard 3dsx format, but without the normal startup code. Instead, a special header is placed at the beginning of the rodata section that includes the plugin name, version, and function pointers that make up the plugin interface. When the plugin host loads a plugin, it calls the plugin's load function and passes in a plugin context and a table of function pointers that the plugin can use to interact with the host.

## Plugin loading
The plugin host attempts to automatically load any 3dsx file in the /plghost/plugins directory as a plugin. Plugins are loaded as follows:

1. Open the 3dsx file
2. Check for the plugin header at the start of the rodata segment
3. Allocate memory for the code, rodata, and data segments
4. Copy the segments from the file to memory
5. Clear the plugin's bss section
5. Apply relocations from the 3dsx file
6. Apply the appropriate memory permissions for each segment
7. Call the plugin's load function

## Example plugin
An example plugin is provided in plugins/test. It demostrates the following functionality:
1. Plugin loading/unloading
2. Calling functions provided by the plugin host
3. Dynamic memory allocation
4. Thread creation
5. Standard file IO

## Possible improvements
### malloc
Extra memory is allocated at the end of each plugin's data segment for the heap. Plugin's can override newlib's sbrk implementation to use the one provided by the plugin host, which will give malloc access to the plugin heap. This implementation uses a fixed size heap for each plugin, which can be wasteful if the plugin does not require all of the memory and can be limiting if the plugin requires more memory. A better method would be to use the kernel to allocate and map memory pages to the end of the data segment as required by the plugin.