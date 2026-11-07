#include "runtime_internal.h"

Logger* logger;

uint64_t __djinn_hash_string(const char* data, uint32_t length)
{
    uint64_t hash = 2166136261u;
    for (uint32_t i = 0; i < length; ++i)
    {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

int64_t __djinn_unix_timestamp_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

uint32_t __djinn_compare_strings(
    char* leftData,
    char* rightData,
    size_t leftLen,
    size_t rightLen
)
{
    if (leftData == rightData) return 1;

    if (!leftData || !rightData)
        return 0;

    if (leftLen != rightLen)
        return 0;

    return memcmp(leftData, rightData, leftLen) == 0;
}

DJINN_ATTR_NONNULL_1
void __djinn_free(void* pointer)
{
    DJINN_TRACE("de-allocating heap memory at %p", pointer);
    free(pointer);
}

DJINN_ATTR_REALLOC
void* __djinn_realloc(void* pointer, size_t new_size)
{
    void* chunk = realloc(pointer, new_size);
    DJINN_ASSERT(chunk, "Out of memory");
    DJINN_TRACE("re-allocated size %zu on heap at %p", new_size, chunk);
    return chunk;
}

DJINN_ATTR_MALLOC
void* __djinn_malloc(size_t size)
{
    DJINN_ASSERT(size > 0, "Size need be greater than zero!");
    void* chunk = calloc(1, size);
    DJINN_ASSERT(chunk, "Out of memory");
    DJINN_TRACE("allocated size %zu on heap at %p", size, chunk);
    return chunk;
}

DJINN_ATTR_MALLOC_ALIGNED
void* __djinn_malloc_aligned(size_t size, size_t alignment)
{
    DJINN_ASSERT(size > 0, "Size need be greater than zero!");
    DJINN_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "Alignment need be a power of two!");
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    void* raw = calloc(1, size + alignment + sizeof(void*));
    DJINN_ASSERT(raw, "Out of memory");
    uintptr_t address = ((uintptr_t)raw + sizeof(void*) + alignment - 1) & ~((uintptr_t)alignment - 1);
    ((void**)address)[-1] = raw;
    DJINN_TRACE("allocated size %zu aligned to %zu on heap at %p", size, alignment, (void*)address);
    return (void*)address;
}

void __djinn_free_aligned(void* pointer)
{
    if (!pointer) return;
    DJINN_TRACE("de-allocating aligned heap memory at %p", pointer);
    free(((void**)pointer)[-1]);
}

#ifdef _WIN32
int64_t __djinn_now_us(void)
{
    static int64_t frequency = 0;
    if (frequency == 0)
    {
        LARGE_INTEGER queried;
        QueryPerformanceFrequency(&queried);
        frequency = queried.QuadPart;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    const int64_t seconds = counter.QuadPart / frequency;
    const int64_t remainder = counter.QuadPart % frequency;
    return seconds * 1000000 + remainder * 1000000 / frequency;
}

int __djinn_argc(void)
{
    return __argc;
}

char* __djinn_argv(int index)
{
    if (index < 0 || index >= __argc || !__argv) return NULL;
    return __argv[index];
}
#else
static int djinn_saved_argc = 0;
static char** djinn_saved_argv = NULL;

// glibc and dyld pass (argc, argv, envp) to initializers in .init_array
__attribute__((constructor)) static void djinn_capture_args(int argc, char** argv, char** envp)
{
    (void)envp;
    djinn_saved_argc = argc;
    djinn_saved_argv = argv;
}

int64_t __djinn_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int __djinn_argc(void)
{
    return djinn_saved_argc;
}

char* __djinn_argv(int index)
{
    if (index < 0 || index >= djinn_saved_argc || !djinn_saved_argv) return NULL;
    return djinn_saved_argv[index];
}
#endif

int __djinn_call_entry(void* address)
{
    return ((int (*)(void))address)();
}

void __djinn_call_method(void* address, void* target)
{
    ((void (*)(void*))address)(target);
}

static void** reflected_types;
static int32_t reflected_type_count;

void __djinn_reflect_register(void** types, int32_t count)
{
    reflected_types = types;
    reflected_type_count = count;
}

int32_t __djinn_reflected_count(void)
{
    return reflected_type_count;
}

void* __djinn_reflected_type(int32_t index)
{
    return reflected_types[index];
}

