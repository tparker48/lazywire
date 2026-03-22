#include "file_ops.h"
#include <filesystem>
#include <fstream>
#include <fcntl.h>

namespace fs = std::filesystem;

bool FileOps::copy_local(const std::string& src, const std::string& dst) {
    try {
        fs::copy(src, dst,
            fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        return true;
    } catch (...) { return false; }
}

bool FileOps::move_local(const std::string& src, const std::string& dst) {
    try {
        fs::rename(src, dst);
        return true;
    } catch (...) { return false; }
}

bool FileOps::delete_local(const std::string& path) {
    try {
        fs::remove_all(path);
        return true;
    } catch (...) { return false; }
}

bool FileOps::rename_local(const std::string& path, const std::string& new_name) {
    try {
        fs::path p(path);
        fs::rename(path, p.parent_path() / new_name);
        return true;
    } catch (...) { return false; }
}

bool FileOps::copy_remote_to_local(SshSession& session, const std::string& remote, const std::string& local) {
    sftp_session sftp = session.sftp();

    sftp_attributes attrs = sftp_stat(sftp, remote.c_str());
    if (!attrs) return false;
    bool is_dir = (attrs->type == SSH_FILEXFER_TYPE_DIRECTORY);
    sftp_attributes_free(attrs);

    if (is_dir) {
        try { fs::create_directories(local); } catch (...) { return false; }
        sftp_dir dir = sftp_opendir(sftp, remote.c_str());
        if (!dir) return false;
        bool ok = true;
        sftp_attributes entry;
        while ((entry = sftp_readdir(sftp, dir)) != nullptr) {
            std::string name = entry->name;
            sftp_attributes_free(entry);
            if (name == "." || name == "..") continue;
            ok = copy_remote_to_local(session, remote + "/" + name, local + "/" + name) && ok;
        }
        sftp_closedir(dir);
        return ok;
    }

    sftp_file file = sftp_open(sftp, remote.c_str(), O_RDONLY, 0);
    if (!file) return false;
    std::ofstream out(local, std::ios::binary);
    if (!out) { sftp_close(file); return false; }
    char buf[65536];
    ssize_t n;
    bool ok = true;
    while ((n = sftp_read(file, buf, sizeof(buf))) > 0)
        out.write(buf, n);
    if (n < 0) ok = false;
    sftp_close(file);
    return ok;
}

bool FileOps::copy_local_to_remote(SshSession& session, const std::string& local, const std::string& remote) {
    sftp_session sftp = session.sftp();

    if (fs::is_directory(local)) {
        sftp_mkdir(sftp, remote.c_str(), 0755); // ignore error if already exists
        bool ok = true;
        try {
            for (const auto& entry : fs::directory_iterator(local)) {
                std::string dst = remote + "/" + entry.path().filename().string();
                ok = copy_local_to_remote(session, entry.path().string(), dst) && ok;
            }
        } catch (...) { return false; }
        return ok;
    }

    std::ifstream in(local, std::ios::binary);
    if (!in) return false;
    sftp_file file = sftp_open(sftp, remote.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!file) return false;
    char buf[65536];
    bool ok = true;
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        if (sftp_write(file, buf, in.gcount()) < 0) { ok = false; break; }
    }
    sftp_close(file);
    return ok;
}

bool FileOps::delete_remote(SshSession& session, const std::string& path) {
    sftp_session sftp = session.sftp();

    // Try as file first
    if (sftp_unlink(sftp, path.c_str()) == SSH_OK) return true;

    // Otherwise recurse into directory
    sftp_dir dir = sftp_opendir(sftp, path.c_str());
    if (!dir) return false;

    bool ok = true;
    sftp_attributes attrs;
    while ((attrs = sftp_readdir(sftp, dir)) != nullptr) {
        std::string name = attrs->name;
        bool is_dir = (attrs->type == SSH_FILEXFER_TYPE_DIRECTORY);
        sftp_attributes_free(attrs);
        if (name == "." || name == "..") continue;
        std::string child = path + "/" + name;
        if (is_dir)
            ok = delete_remote(session, child) && ok;
        else
            ok = (sftp_unlink(sftp, child.c_str()) == SSH_OK) && ok;
    }
    sftp_closedir(dir);
    return (sftp_rmdir(sftp, path.c_str()) == SSH_OK) && ok;
}

bool FileOps::rename_remote(SshSession& session, const std::string& path, const std::string& new_name) {
    // TODO: sftp_rename
    return false;
}

bool FileOps::copy_remote_to_remote(SshSession& src_session, const std::string& src_path,
                                    SshSession& dst_session, const std::string& dst_path) {
    auto tmp = fs::temp_directory_path() / fs::path(src_path).filename();
    if (!copy_remote_to_local(src_session, src_path, tmp.string())) return false;
    bool ok = copy_local_to_remote(dst_session, tmp.string(), dst_path);
    fs::remove_all(tmp);
    return ok;
}
