#pragma once

#include <string>
#include <vector>

// Who the player is when the launcher starts a client: nickname and ship shape.
// Defaults come from ~/.xpilotrc and the account name; the GUI may override both.
class ClientProfile {
public:
    // Reads ~/.xpilotrc and the ship names of shipshapes_path.
    void Load(const char* shipshapes_path);

    // Client arguments for connecting to host:port.
    std::vector<std::string> BuildArgs(int port, const std::string& host = "localhost") const;

    // Bound to the GUI.
    char name[64] = "";                 // empty = use the ~/.xpilotrc nickname
    std::vector<std::string> ship_names;  // names from lib/shipshapes.txt
    int ship_choice = -1;               // index into ship_names, -1 = no -shipShape argument
    bool rc_has_ship = false;           // ~/.xpilotrc defines a ship (else the client uses its built-in one)

private:
    std::string rc_name_;               // nickname read from ~/.xpilotrc
};
