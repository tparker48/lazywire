#pragma once
#include <string>
#include <memory>
#include "pane.h"
#include "ssh_session.h"

enum class ClipboardOp { Copy, Cut };

struct ClipboardEntry {
    ClipboardOp op;
    std::string path;
    std::shared_ptr<SshSession> session; // null = local
};

class FileOps {
public:
    // Local operations
    static bool copy_local(const std::string& src, const std::string& dst);
    static bool move_local(const std::string& src, const std::string& dst);
    static bool delete_local(const std::string& path);
    static bool rename_local(const std::string& path, const std::string& new_name);

    // Remote operations (via SCP/SFTP)
    static bool copy_remote_to_local(SshSession& session, const std::string& remote, const std::string& local);
    static bool copy_local_to_remote(SshSession& session, const std::string& local, const std::string& remote);
    static bool delete_remote(SshSession& session, const std::string& path);
    static bool rename_remote(SshSession& session, const std::string& path, const std::string& new_name);

    // Cross-remote (local as intermediary)
    static bool copy_remote_to_remote(SshSession& src_session, const std::string& src_path,
                                      SshSession& dst_session, const std::string& dst_path);
};
