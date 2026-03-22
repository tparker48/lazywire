#include "local_fs.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

LocalPane::LocalPane(const std::string& start_path) {
    current_path = start_path;
    refresh();
}

std::string LocalPane::title() const {
    return "local: " + current_path;
}

bool LocalPane::navigate(const std::string& path) {
    current_path = path;
    cursor = 0;
    refresh();
    return true;
}

void LocalPane::refresh() {
    entries_.clear();
    load_dir(current_path, 0, entries_);
}

void LocalPane::load_dir(const std::string& path, int depth, std::vector<FileEntry>& out) {
    std::vector<FileEntry> entries;
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            FileEntry fe;
            fe.name = entry.path().filename().string();
            fe.full_path = entry.path().string();
            fe.is_dir = entry.is_directory() && !entry.is_symlink();
            fe.depth = depth;
            entries.push_back(fe);
        }
    } catch (...) {
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
    });

    out.insert(out.end(), entries.begin(), entries.end());
}

void LocalPane::toggle_expand(int index) {
    if (index < 0 || index >= (int)entries_.size()) return;
    if (!entries_[index].is_dir) return;

    if (entries_[index].expanded) {
        int depth = entries_[index].depth;
        auto it = entries_.begin() + index + 1;
        while (it != entries_.end() && it->depth > depth) {
            it = entries_.erase(it);
        }
        entries_[index].expanded = false;
    } else {
        std::vector<FileEntry> children;
        load_dir(entries_[index].full_path, entries_[index].depth + 1, children);
        entries_.insert(entries_.begin() + index + 1, children.begin(), children.end());
        entries_[index].expanded = true;
    }
}
