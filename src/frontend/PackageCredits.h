// PC EXTENSION — third-party credit lines of a package build (not in the original game).
// The NVIDIA DLSS credit (user 2026-10-12: start-up splash, About if any, credits; "clean and unobtrusive", non-commercial
// project): one small line in the bottom-right corner while the start-up movie is up and while the credits movie plays.
// The original has no About screen, so there is none here. Compiled in only for DLSS package builds (CMake
// -DWFC_DLSS_PACKAGE=ON, configured by Systems' DLSS package build); every other build draws nothing and keeps the
// original frontend unchanged.
#pragma once
#include <cctype>
#include <string>

#ifndef WFC_DLSS_PACKAGE
#define WFC_DLSS_PACKAGE 0
#endif

namespace frontend {

constexpr bool kDlssPackage = WFC_DLSS_PACKAGE != 0;
// NVIDIA's attribution text (no logo artwork).
constexpr const char* kDlssCreditText =
    "NVIDIA DLSS: Copyright (c) NVIDIA Corporation. NVIDIA, the NVIDIA logo and DLSS are trademarks of NVIDIA Corporation.";

// Game.PlayMovie of the Extras menu's Credits entry (FMV_credits).
inline bool isCreditsMovie(const std::string& movie) {
    std::string l;
    for (char c : movie) l += (char)std::tolower((unsigned char)c);
    return l.find("credits") != std::string::npos;
}

// When the credit line is up: the start-up movie (boot) or the credits movie, and only in a DLSS package build.
inline bool packageCreditShown(bool package, bool startupMovieUp, bool movieUp, const std::string& movie) {
    return package && (startupMovieUp || (movieUp && isCreditsMovie(movie)));
}

} // namespace frontend
