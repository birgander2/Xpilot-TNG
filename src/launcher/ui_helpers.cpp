#include "ui_helpers.h"

#include <filesystem>

namespace fs = std::filesystem;

std::string ShellQuote(const std::string& a) {
    if (!a.empty() && a.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                                          "_@%+=:,./-") == std::string::npos)
        return a;
    std::string q = "'";
    for (char c : a) {
        if (c == '\'') q += "'\\''";
        else q += c;
    }
    return q + "'";
}

std::string CommandLine(const std::string& bin, const std::vector<std::string>& args) {
    std::error_code ec;
    std::string cmd = fs::relative(bin, fs::current_path(ec), ec).string();
    if (ec || cmd.empty() || cmd.rfind("..", 0) == 0) cmd = bin;   // keep absolute outside the tree
    if (cmd.find('/') == std::string::npos && fs::exists(cmd)) cmd = "./" + cmd;
    for (const auto& a : args) cmd += ' ' + ShellQuote(a);
    return cmd;
}

void StatusDot(const ImVec4& col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    dl->AddCircleFilled(ImVec2(p.x + 6, p.y + h * 0.5f), 5.0f, ImGui::ColorConvertFloat4ToU32(col));
    ImGui::Dummy(ImVec2(16, h));
    ImGui::SameLine();
}

bool GreenButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.45f, 0.20f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

bool AnyPopupOpen() { return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId); }

bool HotkeyPressed(ImGuiKey key) {
    return !ImGui::GetIO().WantTextInput && !AnyPopupOpen() && ImGui::IsKeyPressed(key, false);
}
