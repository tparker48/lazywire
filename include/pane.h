#pragma once
#include <string>
#include <vector>
#include <memory>

enum class PaneType { Local, Remote };

struct FileEntry {
    std::string name;
    std::string full_path;
    bool is_dir = false;
    bool expanded = false;
    int depth = 0;
};

class Pane {
public:
    virtual ~Pane() = default;
    virtual PaneType type() const = 0;
    virtual std::string title() const = 0;
    virtual bool navigate(const std::string& path) = 0;
    virtual const std::vector<FileEntry>& entries() const = 0;
    virtual void toggle_expand(int) {}
    virtual void refresh() {}

    int cursor = 0;
    std::string current_path;
    std::string search_query;
};
