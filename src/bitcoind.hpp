#pragma once

#include <functional>
#include <string>

// Called with each line of output from bitcoind's stdout/stderr.
using OutputCallback = std::function<void(const std::string& line)>;

// Returns the full path to bitcoind if found in PATH, or empty string if not.
std::string find_bitcoind();

// Launch bitcoind with -daemonwait. Calls on_output for each line of
// stdout/stderr as it arrives. Blocks until the process exits.
// Returns the exit code (0 = success).
int launch_bitcoind(const std::string& cmd, const std::string& datadir, const std::string& network,
                    const OutputCallback& on_output);
