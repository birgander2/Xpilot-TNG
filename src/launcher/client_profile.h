#pragma once

#include <string>
#include <vector>

// Value of "xpilot.<key>" in the xpilotrc, empty if missing.
std::string XpilotrcValue(const char* key);

// Client voice chat settings (client options voice*), see doc/VOICECHAT.md.
struct VoiceSettings {
    bool chat = true;
    bool activation = false;        // false = push-to-talk
    bool activation_team = true;
    int activation_level = -40;     // dB
    int volume = 100;               // %
    int mic_gain = 100;             // %
    int bitrate = 24;               // kbit/s
    int team_filter = 1;            // 0 = off, 1 = radio, 2 = telephone
    int all_filter = 0;
};

// Who the player is when the launcher starts a client: nickname and ship shape.
// Defaults come from the xpilotrc and the account name; the GUI may override both.
class ClientProfile {
public:
    // Reads the xpilotrc and the ship names of shipshapes_path.
    void Load(const char* shipshapes_path);

    // Client arguments for connecting to host:port.
    std::vector<std::string> BuildArgs(int port, const std::string& host = "localhost") const;

    // Bound to the GUI.
    char name[64] = "";                 // empty = use the the xpilotrc nickname
    std::vector<std::string> ship_names;  // names from lib/shipshapes.txt
    int ship_choice = -1;               // index into ship_names, -1 = no -shipShape argument
    bool rc_has_ship = false;           // the xpilotrc defines a ship (else the client uses its built-in one)
    bool newbie = true;                 // newbie help; preset from the xpilotrc, else the client default (true)
    VoiceSettings voice;                // preset from the xpilotrc, else the client defaults

private:
    std::string rc_name_;               // nickname read from the xpilotrc
    VoiceSettings rc_voice_;            // voice as loaded; only changes are passed on
};
