#include "app/diag/mem_stats.h"
#include "support/test.h"

#include <string>

TEST("memory: heap statistics include a live allocation") {
    if (diag::sampleMem().heapKb < 0)
        return; // This platform does not expose heap statistics.
    constexpr size_t  bytes = 2 * 1024 * 1024;
    const std::string allocation(bytes, 'm');
    // In particular, Clang's ASan allocator must not be read through mallinfo2.
    CHECK(diag::sampleMem().heapKb >= long(bytes / 1024));
    CHECK(allocation.front() == 'm' && allocation.back() == 'm');
}
