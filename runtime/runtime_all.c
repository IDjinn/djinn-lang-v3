// Single translation unit of the whole runtime, used for the prebuilt library.
// The JIT (RuntimeDyld on Windows) cannot resolve 32-bit relative data references between
// separately loaded objects, so the library ships the runtime as one object.
#include "djinn_runtime.c"
#include "scheduler.c"
#include "io_file.c"
#include "io_socket.c"
#include "diagnostics.c"
#include "threads.c"
#include "console.c"
#include "filesystem.c"
#include "process.c"
#include "int128.c"
