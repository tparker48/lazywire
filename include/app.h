#pragma once
#include <vector>
#include <memory>
#include <string>
#include "pane.h"
#include "file_ops.h"
#include "ssh_session.h"
#include <atomic>
#include <mutex>
#include <functional>

struct ConnectOverlay {
    bool active = false;
    std::string user_host; // "user@host"
    std::string password;
    int field = 0;         // 0 = user@host, 1 = password
    std::string error;
};

struct ConfirmOverlay {
    bool active = false;
    std::string message;
    std::function<void()> on_confirm;
};

struct MkdirOverlay {
    bool active = false;
    std::string parent_path;
    std::string name;
    bool is_remote = false;
    std::shared_ptr<SshSession> session;
};

struct RenameOverlay {
    bool active = false;
    std::string original_path;
    std::string new_name;
    bool is_remote = false;
    std::shared_ptr<SshSession> session; // null = local
};

struct QueuedOp {
    enum class Type { Copy, Move, Delete, Rename, Mkdir } type;
    std::string src;
    std::string dst;      // empty for Delete; new_name for Rename
    std::shared_ptr<SshSession> src_session; // null = local
    std::shared_ptr<SshSession> dst_session; // null = local
};

struct PaneState {
    std::unique_ptr<Pane> pane;
    int scroll_offset = 0;
    bool fuzzy_mode = false;
    std::string fuzzy_query;
    std::vector<std::string> fuzzy_candidates;
    std::vector<std::string> fuzzy_results;
    int fuzzy_cursor = 0;
    bool jump_mode = false;
    std::string jump_query;
    int jump_match = 0; // which match we're cycling through
};

class App {
public:
    App();
    App(const std::string& user, const std::string& host, const std::string& password);
    int run();
private:
    std::vector<PaneState> panes_;
    int active_pane_ = 0;
    std::string start_path_;
    ClipboardEntry clipboard_;
    bool clipboard_active_ = false;
    std::vector<QueuedOp> op_queue_;
    bool batch_mode_ = false;
    std::string last_error_;
    ConnectOverlay overlay_;
    RenameOverlay rename_overlay_;
    MkdirOverlay mkdir_overlay_;
    ConfirmOverlay confirm_overlay_;
    std::atomic<bool> loading_{false};
    std::atomic<bool> loading_cancelled_{false};
    std::string loading_msg_;
    std::atomic<int> spinner_frame_{0};
    std::mutex result_mutex_;
    std::function<void()> on_load_complete_;
};
