// Observe the tested threads, not a process-wide count that unrelated exits can change.
#pragma once

#ifdef __linux__
#include <vector>

namespace base::test {

std::vector<int> liveThreadIds();
std::vector<int> newThreadIds(const std::vector<int> &before);
bool             threadsExited(const std::vector<int> &ids);

} // namespace base::test
#endif
