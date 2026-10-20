#pragma once

#include <map>
#include <string>
#include <vector>

// Editor for the player profile in the xpilotrc: nickname, ship, key bindings,
// mouse buttons, weapon modifier banks and audio (game sounds, voice chat).
class ProfileEditor {
public:
    // Reads the xpilotrc. ship_names are the choices of lib/shipshapes.txt.
    // keep_audio: keep audio edits that were not saved yet (they stay "unsaved").
    void Load(const std::vector<std::string>& ship_names, bool keep_audio = false);

    // Client arguments for audio values that were changed but not saved to the
    // xpilotrc, so they apply to the next game anyway.
    std::vector<std::string> UnsavedAudioArgs() const;

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
    bool loaded_ = false;

    std::map<std::string, std::string> rc_;      // values defined in the xpilotrc when loaded
    std::map<std::string, std::string> shown_;   // what the editor showed after loading

    int capture_ = -1;                           // key row waiting for a key press
    bool capture_add_ = false;
    int cancelled_frame_ = -1;
    std::string status_;
    bool status_error_ = false;
};
