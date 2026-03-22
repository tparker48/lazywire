#pragma once
#include <string>
#include <vector>

// Wraps fzf --filter for fuzzy matching. Requires fzf installed locally.
namespace fuzzy {
    // Filter and rank candidates against query using fzf. Returns ranked results.
    std::vector<std::string> search(const std::vector<std::string>& candidates, const std::string& query);
}
