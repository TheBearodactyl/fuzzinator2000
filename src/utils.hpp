#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

template<typename T>
using Vec = std::vector<T>;

template<typename T>
using Option = std::optional<T>;

using String = std::string;
using StringV = std::string_view;

#define needle_haysack std::string_view needle, std::string_view haystack
