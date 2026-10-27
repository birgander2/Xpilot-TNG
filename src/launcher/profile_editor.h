#pragma once

#include <map>
#include <string>
#include <vector>

class ClientProfile;

// Editor for the player profile in the xpilotrc: nickname, ship, key bindings,
// mouse buttons, weapon modifier banks, audio (game sounds, voice chat) and
// network settings (jitter buffer, turn prediction).
class ProfileEditor {
public:
    // Reads the xpilotrc. ship_names are the choices of lib/shipshapes.txt.
    // keep_unsaved: keep all edits that were not saved yet (they stay "unsaved").
    void Load(const std::vector<std::string>& ship_names, bool keep_unsaved = false);

    // Client arguments for every value that was changed but not saved to the
    // xpilotrc (keys, mouse buttons, modifier banks, audio, network), so they
    // apply to the next game anyway. Name and ship: see ApplyIdentityTo().
    std::vector<std::string> UnsavedArgs() const;

    // Copies an unsaved nickname / ship choice into the profile the launcher
    // starts clients with.
    void ApplyIdentityTo(ClientProfile& profile) const;

    // Draws the editor into the current window. Returns true when the xpilotrc was written.
    bool Render(float bottom_reserve);

    // True while a key is being captured (Esc cancels the capture, not the screen).
    bool Capturing() const;

    // Unsaved edits? Save() writes them to the xpilotrc (true on success).
    bool Dirty() const;
    bool Save();

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
    void RenderAudio();
    void RenderNetwork();
    std::vector<std::pair<std::string, std::string>> Changes() const;

    static constexpr int kButtons = 5, kBanks = 4;

    char name_[64] = "";
    std::vector<std::string> ship_names_;
    int ship_ = -1;                              // -1 = leave the xpilotrc ship alone
    std::string rc_ship_;                        // ship (name) currently in the xpilotrc

    std::vector<std::string> key_values_;        // per key action: space separated key names
    std::vector<std::string> button_actions_[kButtons];
    ModBank banks_[kBanks];

    // audio: client options sound, maxVolume, voice*
    struct Audio {
        bool sound = true;
        int max_volume = 100;            // game sounds, %
        bool voice_chat = true;
        bool voice_activation = true;    // false = push-to-talk only
        bool voice_activation_team = true;
        int voice_level = -40;           // dB
        int voice_volume = 100;          // %
        int voice_mic_gain = 100;        // %
    } audio_;
    std::vector<std::pair<std::string, std::string>> AudioValues() const;

    // network: client options maxJitterBuffer, dirPrediction, keyTurnMouse, keyTurnRate
    struct Network {
        bool jitter_buffer = true;       // maxJitterBuffer > 0
        int jitter_ms = 150;             // upper limit of the buffer delay
        bool mouse_prediction = true;    // dirPrediction
        bool key_turn_mouse = false;     // keyTurnMouse
        int key_turn_rate = 630;         // keyTurnRate, degrees per second
    } net_;
    std::vector<std::pair<std::string, std::string>> NetworkValues() const;
    bool loaded_ = false;

    std::map<std::string, std::string> rc_;      // values defined in the xpilotrc when loaded
    std::map<std::string, std::string> shown_;   // what the editor showed after loading

    int capture_ = -1;                           // key row waiting for a key press
    bool capture_add_ = false;
    int cancelled_frame_ = -1;
    std::string status_;
    bool status_error_ = false;
};
