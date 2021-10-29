#ifndef SZKARC_H
#define SZKARC_H
#include <cstdint>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <filesystem>

int get_physical_core_counts();
int32_t stream_os_open(void* stream, const std::filesystem::path& path, int32_t mode);

template <typename T>
std::vector<T> flatten_nested(const std::vector<std::vector<T>>& nested) {
  using ListType = std::vector<T>;
  size_t total_size = std::transform_reduce(nested.cbegin(), nested.cend(), 0u, std::plus{}, [](const ListType& l) {return l.size(); });
  ListType flat;
  flat.reserve(total_size);
  for (auto& subd : nested) {
    flat.insert(
      flat.end(),
      std::make_move_iterator(subd.begin()),
      std::make_move_iterator(subd.end())
    );
  }
  return flat;
}

#ifdef _WIN32
std::string wstr2utf8(std::wstring const& src);
#endif
#endif /* SZKARC_H */
