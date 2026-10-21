#pragma once

#include <string>
#include <vector>

// Value of "xpilot.<key>" in the xpilotrc, empty if missing.
std::string XpilotrcValue(const char* key);

// Who the player is when the launcher starts a client: nickname and ship shape.
// Defaults come from the xpilotrc and the account name; the GUI may override both.
class ClientProfile {
public:
    // Reads the xpilotrc and the ship names of shipshapes_path.
    void Load(const char* shipshapes_path);

    // Client arguments for connecting to host:port.
    // extra: further client options, e.g. settings not saved to the xpilotrc yet.
    std::vector<std::string> BuildArgs(int port, const std::string& host = "localhost",
                                       const std::vector<std::string>& extra = {}) const;

    // Bound to the GUI.
    char name[64] = "";                 // empty = use the the xpilotrc nickname
    std::vector<std::string> ship_names;  // names from lib/shipshapes.txt
    int ship_choice = -1;               // index into ship_names, -1 = no -shipShape argument
    bool rc_has_ship = false;           // the xpilotrc defines a ship (else the client uses its built-in one)
    bool newbie = true;                 // newbie help; preset from the xpilotrc, else the client default (true)

private:
    std::string rc_name_;               // nickname read from the xpilotrc
};
