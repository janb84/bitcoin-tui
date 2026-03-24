#include "bitcoind.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

std::string find_bitcoind() {
    const char* path_env = getenv("PATH");
    if (!path_env)
        return {};

    std::string            path(path_env);
    std::string::size_type start = 0;
    while (start < path.size()) {
        auto end = path.find(':', start);
        if (end == std::string::npos)
            end = path.size();
        std::string candidate = path.substr(start, end - start) + "/bitcoind";
        struct stat st{};
        if (stat(candidate.c_str(), &st) == 0 && (st.st_mode & S_IXUSR))
            return candidate;
        start = end + 1;
    }
    return {};
}

int launch_bitcoind(const std::string& cmd, const std::string& datadir, const std::string& network,
                    const OutputCallback& on_output) {
    // Build argv.
    std::vector<std::string> args_storage;
    args_storage.push_back(cmd);
    args_storage.push_back("-daemonwait");
    if (!datadir.empty())
        args_storage.push_back("-datadir=" + datadir);
    if (network == "testnet3")
        args_storage.push_back("-testnet");
    else if (network == "signet")
        args_storage.push_back("-signet");
    else if (network == "regtest")
        args_storage.push_back("-regtest");

    // Report the command line.
    std::string cmdline;
    for (const auto& s : args_storage) {
        if (!cmdline.empty())
            cmdline += ' ';
        cmdline += s;
    }

    std::vector<char*> argv;
    for (auto& s : args_storage)
        argv.push_back(s.data());
    argv.push_back(nullptr);

    // Create a pipe to capture stdout/stderr.
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child: redirect stdout and stderr to the pipe write end.
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execvp(argv[0], argv.data());
        // If execvp returns, it failed.
        std::string           err = std::string(argv[0]) + ": " + strerror(errno) + "\n";
        [[maybe_unused]] auto _   = write(STDERR_FILENO, err.c_str(), err.size());
        _exit(127);
    }

    // Parent: read from pipe until EOF, then wait for child.
    close(pipefd[1]);

    std::string line;
    char        buf[256];
    ssize_t     n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                on_output(line);
                line.clear();
            } else {
                line += buf[i];
            }
        }
    }
    if (!line.empty())
        on_output(line);
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
