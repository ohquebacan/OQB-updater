#pragma once

#include <string>

// Decides where the contents of a downloaded zip belong.
namespace ziplayout {

    struct Layout
    {
        std::string dir;    // extract below this directory ("/" or the fallback)
        std::string strip;  // wrapper folder to cut off ("" = none)
    };

    // Zip with SD-card folders (switch/, atmosphere/, config/, themes/ ... possibly inside one
    // wrapper folder like "sd_out/") -> "/". Anything else -> fallbackDir.
    Layout detect(const std::string& archivePath, const std::string& fallbackDir);

}  // namespace ziplayout
