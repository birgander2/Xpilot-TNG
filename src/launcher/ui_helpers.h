#pragma once

#include <string>
#include <vector>
#include <imgui.h>

// Quotes a command line argument for POSIX shells (the text is meant to be pasted into one).
std::string ShellQuote(const std::string& arg);

// "<bin> <args...>" for display and copying; the binary is shown relative to the working directory.
std::string CommandLine(const std::string& bin, const std::vector<std::string>& args);

// Small filled circle in front of a status text.
void StatusDot(const ImVec4& color);

// Green primary action button; returns true when clicked.
bool GreenButton(const char* label, const ImVec2& size);

bool AnyPopupOpen();
// Single key shortcut that must not fire while typing or while a popup is open.
bool HotkeyPressed(ImGuiKey key);
