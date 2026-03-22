#include "fuzzy.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>

namespace fuzzy {

std::vector<std::string> search(const std::vector<std::string>& candidates, const std::string& query) {
    if (query.empty()) return candidates;

    int stdin_fds[2], stdout_fds[2];
    if (pipe(stdin_fds) < 0 || pipe(stdout_fds) < 0) return {};

    pid_t pid = fork();
    if (pid < 0) return {};

    if (pid == 0) {
        dup2(stdin_fds[0], STDIN_FILENO);
        dup2(stdout_fds[1], STDOUT_FILENO);
        close(stdin_fds[0]);
        close(stdin_fds[1]);
        close(stdout_fds[0]);
        close(stdout_fds[1]);

        std::string filter_arg = "--filter=" + query;
        const char* argv[] = {"fzf", filter_arg.c_str(), nullptr};
        execvp("fzf", const_cast<char* const*>(argv));
        _exit(1);
    }

    close(stdin_fds[0]);
    close(stdout_fds[1]);

    for (const auto& c : candidates) {
        std::string line = c + "\n";
        write(stdin_fds[1], line.c_str(), line.size());
    }
    close(stdin_fds[1]);

    std::string output;
    char buf[4096];
    ssize_t n;
    while ((n = read(stdout_fds[0], buf, sizeof(buf))) > 0) {
        output.append(buf, n);
    }
    close(stdout_fds[0]);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) return {};

    std::vector<std::string> results;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty()) results.push_back(line);
    }
    return results;
}

} // namespace fuzzy
