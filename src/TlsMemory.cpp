// mbedTLS memory in PSRAM.
//
// The prebuilt Arduino libraries are compiled with CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC,
// so every HTTPS request took its TLS buffers (two 16 KB records plus the handshake)
// from the ~70 KB of internal RAM, which dropped to 16 KB free during requests.
// sdkconfig.defaults cannot change that: with `framework = arduino` it is not read.
//
// platformio.ini links with -Wl,--wrap=esp_mbedtls_mem_calloc, which sends mbedTLS's
// allocations here. This is what CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC does, with internal
// RAM as a fallback when PSRAM is full. Freeing needs no change: esp_mbedtls_mem_free
// is heap_caps_free, which takes memory from either place.

#include <esp_heap_caps.h>
#include <stddef.h>

extern "C" void* __wrap_esp_mbedtls_mem_calloc(size_t n, size_t size) {
    void* memory = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) memory = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return memory;
}
