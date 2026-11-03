#pragma once

#include <string>
#include <vector>

// Value of "xpilot.<key>" in the xpilotrc, empty if missing.
std::string XpilotrcValue(const char* key);

// Who the player is when the launcher starts a client: nickname and ship shape.
// Defaults come from the xpilotrc and the account name; the nickname may be overridden
// in the GUI, the ship only by an unsaved choice in the profile editor.
class ClientProfile {
public:
    // Reads the nickname and newbie setting from the xpilotrc.
    void Load();

    // Client arguments for connecting to host:port.
    // extra: further client options, e.g. settings not saved to the xpilotrc yet.
    std::vector<std::string> BuildArgs(int port, const std::string& host = "localhost",
                                       const std::vector<std::string>& extra = {}) const;

    // Bound to the GUI.
    char name[64] = "";                 // empty = use the the xpilotrc nickname
    std::string ship_shape;             // unsaved profile edit: full shape definition; empty = the client reads the xpilotrc
    bool newbie = true;                 // newbie help; preset from the xpilotrc, else the client default (true)

private:
    std::string rc_name_;               // nickname read from the xpilotrc
};
