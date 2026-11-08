#include "support/linux_threads.h"

#ifdef __linux__
#include <algorithm>
#include <cstdlib>
#include <dirent.h>

namespace base::test {

std::vector<int> liveThreadIds() {
    std::vector<int> ids;
    if (DIR *d = opendir("/proc/self/task")) {
        while (const dirent *e = readdir(d))
            if (e->d_name[0] != '.')
                ids.push_back(std::atoi(e->d_name));
        closedir(d);
    }
    return ids;
}

std::vector<int> newThreadIds(const std::vector<int> &before) {
    auto ids = liveThreadIds();
    std::erase_if(ids, [&](int id) {
        return std::find(before.begin(), before.end(), id) != before.end();
    });
    return ids;
}

bool threadsExited(const std::vector<int> &ids) {
    const auto live = liveThreadIds();
    return !live.empty() && std::none_of(ids.begin(), ids.end(), [&](int id) {
        return std::find(live.begin(), live.end(), id) != live.end();
    });
}

} // namespace base::test
#endif
