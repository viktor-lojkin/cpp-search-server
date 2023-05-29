#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <set>


//Формируем вектор из строки с пробелами
std::vector<std::string> SplitIntoWords(const std::string& text);
std::vector<std::string_view> SplitIntoWords(std::string_view text);


template <typename StringContainer>
std::set<std::string, std::less<>> MakeUniqueNonEmptyStrings(const StringContainer& text) {
    std::set<std::string, std::less<>> non_empty_strings;
    for (std::string_view sv : text) {
        if (!sv.empty()) {
            non_empty_strings.insert({ sv.data(), sv.size() });
        }
    }
    return non_empty_strings;
}