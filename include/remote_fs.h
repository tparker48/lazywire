#pragma once
#include "pane.h"
#include "ssh_session.h"
#include <memory>

class RemotePane : public Pane {
public:
    RemotePane(std::shared_ptr<SshSession> session, const std::string& start_path);
    PaneType type() const override { return PaneType::Remote; }
    std::string title() const override;
    bool navigate(const std::string& path) override;
    const std::vector<FileEntry>& entries() const override { return entries_; }

    void toggle_expand(int index) override;
    void refresh() override;
    std::shared_ptr<SshSession> session_ptr() const { return session_; }

private:
    void load_dir(const std::string& path, int depth, std::vector<FileEntry>& out);
    std::shared_ptr<SshSession> session_;
    std::vector<FileEntry> entries_;
};
