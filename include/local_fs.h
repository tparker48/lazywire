#pragma once
#include "pane.h"

class LocalPane : public Pane {
public:
    explicit LocalPane(const std::string& start_path);
    PaneType type() const override { return PaneType::Local; }
    std::string title() const override;
    bool navigate(const std::string& path) override;
    const std::vector<FileEntry>& entries() const override { return entries_; }

    void toggle_expand(int index) override;
    void refresh() override;

private:
    void load_dir(const std::string& path, int depth, std::vector<FileEntry>& out);
    std::vector<FileEntry> entries_;
};
