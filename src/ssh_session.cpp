#include "ssh_session.h"
#include <stdexcept>

SshSession::SshSession(const std::string& host, const std::string& user, int port)
    : host_(host), user_(user), port_(port) {}

SshSession::~SshSession() {
    if (sftp_) sftp_free(sftp_);
    if (session_) {
        ssh_disconnect(session_);
        ssh_free(session_);
    }
}

bool SshSession::connect() {
    session_ = ssh_new();
    if (!session_) return false;

    ssh_options_set(session_, SSH_OPTIONS_HOST, host_.c_str());
    ssh_options_set(session_, SSH_OPTIONS_USER, user_.c_str());
    ssh_options_set(session_, SSH_OPTIONS_PORT, &port_);

    if (ssh_connect(session_) != SSH_OK) return false;

    // Authenticate via SSH agent / key
    if (ssh_userauth_autopubkey(session_, nullptr) != SSH_AUTH_SUCCESS) return false;

    sftp_ = sftp_new(session_);
    if (!sftp_) return false;
    if (sftp_init(sftp_) != SSH_OK) return false;

    detect_os();
    return true;
}

bool SshSession::connect_password(const std::string& password) {
    session_ = ssh_new();
    if (!session_) return false;

    ssh_options_set(session_, SSH_OPTIONS_HOST, host_.c_str());
    ssh_options_set(session_, SSH_OPTIONS_USER, user_.c_str());
    ssh_options_set(session_, SSH_OPTIONS_PORT, &port_);

    if (ssh_connect(session_) != SSH_OK) return false;

    if (ssh_userauth_password(session_, nullptr, password.c_str()) != SSH_AUTH_SUCCESS)
        return false;

    sftp_ = sftp_new(session_);
    if (!sftp_) return false;
    if (sftp_init(sftp_) != SSH_OK) return false;

    detect_os();
    return true;
}

bool SshSession::exec(const std::string& cmd, std::string& output) const {
    ssh_channel ch = ssh_channel_new(session_);
    if (!ch) return false;
    if (ssh_channel_open_session(ch) != SSH_OK) {
        ssh_channel_free(ch);
        return false;
    }
    if (ssh_channel_request_exec(ch, cmd.c_str()) != SSH_OK) {
        ssh_channel_close(ch);
        ssh_channel_free(ch);
        return false;
    }
    char buf[4096];
    int n;
    while ((n = ssh_channel_read(ch, buf, sizeof(buf), 0)) > 0)
        output.append(buf, n);
    ssh_channel_send_eof(ch);
    ssh_channel_close(ch);
    ssh_channel_free(ch);
    return true;
}

bool SshSession::is_connected() const {
    return session_ && ssh_is_connected(session_);
}

void SshSession::detect_os() {
    std::string out;
    // uname works on Linux/Mac; fails or returns empty on Windows
    exec("uname 2>/dev/null", out);
    is_windows_ = out.empty();
}
