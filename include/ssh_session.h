#pragma once
#include <string>
#include <memory>
#include <libssh/libssh.h>
#include <libssh/sftp.h>

class SshSession {
public:
    SshSession(const std::string& host, const std::string& user, int port = 22);
    ~SshSession();

    bool connect();
    bool connect_password(const std::string& password);
    bool is_connected() const;
    // Run a command and return stdout. Returns false on failure.
    bool exec(const std::string& cmd, std::string& output) const;
    ssh_session raw() const { return session_; }
    sftp_session sftp() const { return sftp_; }

    const std::string& host() const { return host_; }
    const std::string& user() const { return user_; }
    bool is_windows() const { return is_windows_; }

private:
    void detect_os();  // sets is_windows_
    std::string host_;
    std::string user_;
    int port_;
    ssh_session session_ = nullptr;
    sftp_session sftp_ = nullptr;
    bool is_windows_ = false;
};
