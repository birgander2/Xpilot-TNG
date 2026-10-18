#pragma once

#include <map>
#include <string>
#include <vector>

// Editor for the player profile in the xpilotrc: nickname, ship, key bindings,
// mouse buttons and weapon modifier banks.
class ProfileEditor {
public:
    // Reads the xpilotrc. ship_names are the choices of lib/shipshapes.txt.
    void Load(const std::vector<std::string>& ship_names);

    // Draws the editor into the current window. Returns true when the xpilotrc was written.
    bool Render(float bottom_reserve);

    // True while a key is being captured (Esc cancels the capture, not the screen).
    bool Capturing() const;

private:
    struct ModBank {
        int nuclear = 0;      // 0 off, 1 nuclear, 2 full nuclear
        bool cluster = false, implosion = false;
        int laser = 0;        // 0 off, 1 stun, 2 blind
        int velocity = 0, mini = 1, spread = 0, power = 0;   // mini: 1 = off, 2..4 = number of minis
        static ModBank Parse(const std::string& s);
        std::string Serialize() const;
    };

    void RenderIdentity();
    void RenderKeys();
    void RenderMouse();
    void RenderModBanks();
    bool Save();
    bool Dirty() const;
    std::vector<std::pair<std::string, std::string>> Changes() const;

    static constexpr int kButtons = 5, kBanks = 4;

    char name_[64] = "";
    std::vector<std::string> ship_names_;
    int ship_ = -1;                              // -1 = leave the xpilotrc ship alone
    std::string rc_ship_;                        // ship (name) currently in the xpilotrc

    std::vector<std::string> key_values_;        // per key action: space separated key names
    std::vector<std::string> button_actions_[kButtons];
    ModBank banks_[kBanks];

    std::map<std::string, std::string> rc_;      // values defined in the xpilotrc when loaded
    std::map<std::string, std::string> shown_;   // what the editor showed after loading

    int capture_ = -1;                           // key row waiting for a key press
    bool capture_add_ = false;
    int cancelled_frame_ = -1;
    std::string status_;
    bool status_error_ = false;
};
