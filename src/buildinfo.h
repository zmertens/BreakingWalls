#ifndef BW_BUILDINFO_H
#define BW_BUILDINFO_H

#include <string_view>

/// @file buildinfo.h
/// @brief Build information for the application
/// @namespace bw
namespace bw
{
    /// @brief Build information for the application
    struct buildinfo
    {
        static constexpr std::string_view CommitSHA = "'bc492db'";

        static constexpr std::string_view Timestamp = "2026-03-14T12:54:52";
        
        static constexpr std::string_view Version = "0.6.5";
    };

}

#endif // BW_BUILDINFO_H

