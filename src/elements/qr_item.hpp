#pragma once

#include <string>
#include <vector>

struct QrItem {
    std::string label; // tab label (empty = no tab bar)
    std::string data;  // text to encode as QR
};

using QrItems = std::vector<QrItem>;
