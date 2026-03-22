#include "remote_fs.h"
#include <algorithm>

RemotePane::RemotePane(std::shared_ptr<SshSession> session, const std::string& start_path)
    : session_(std::move(session)) {
    current_path = start_path;
    refresh();
}

std::string RemotePane::title() const {
    return session_->user() + "@" + session_->host() + ":" + current_path;
}

bool RemotePane::navigate(const std::string& path) {
    current_path = path;
    cursor = 0;
    refresh();
    return true;
}

void RemotePane::refresh() {
    entries_.clear();
    load_dir(current_path, 0, entries_);
}

void RemotePane::toggle_expand(int index) {
    if (index < 0 || index >= (int)entries_.size()) return;
    if (!entries_[index].is_dir) return;

    if (entries_[index].expanded) {
        int depth = entries_[index].depth;
        auto it = entries_.begin() + index + 1;
        while (it != entries_.end() && it->depth > depth)
            it = entries_.erase(it);
        entries_[index].expanded = false;
    } else {
        std::vector<FileEntry> children;
        load_dir(entries_[index].full_path, entries_[index].depth + 1, children);
        entries_.insert(entries_.begin() + index + 1, children.begin(), children.end());
        entries_[index].expanded = true;
    }
}

void RemotePane::load_dir(const std::string& path, int depth, std::vector<FileEntry>& out) {
    sftp_dir dir = sftp_opendir(session_->sftp(), path.c_str());
    if (!dir) return;

    std::vector<FileEntry> entries;
    sftp_attributes attrs;
    while ((attrs = sftp_readdir(session_->sftp(), dir)) != nullptr) {
        std::string name = attrs->name;
        if (name == "." || name == "..") { sftp_attributes_free(attrs); continue; }

        FileEntry fe;
        fe.name = name;
        // Windows drive letters (e.g. "C:") need a trailing slash: /C:/
        bool is_drive = (name.size() == 2 && name[1] == ':');
        std::string sep = (path == "/" && is_drive) ? "" : "/";
        fe.full_path = path + sep + name + (is_drive ? "/" : "");
        fe.is_dir = (attrs->type == SSH_FILEXFER_TYPE_DIRECTORY);
        fe.depth = depth;
        entries.push_back(fe);
        sftp_attributes_free(attrs);
    }
    sftp_closedir(dir);

    std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
    });

    out.insert(out.end(), entries.begin(), entries.end());
}
