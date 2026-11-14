#pragma once

#include <map>
#include <string>
#include <vector>

#include "ship_files.h"
#include "ship_widgets.h"

class ClientProfile;

// Editor for the player profile in the xpilotrc: nickname, ship, key bindings,
// mouse buttons, weapon modifier banks, audio (game sounds, voice chat),
// network settings (jitter buffer, turn prediction) and visual effects.
class ProfileEditor {
public:
    // Reads the xpilotrc and the ships (stock_ships_path = lib/shipshapes.cfg, plus the
    // personal collection). keep_unsaved: keep all edits that were not saved yet
    // (they stay "unsaved").
    void Load(const std::string& stock_ships_path, bool keep_unsaved = false);

    // The "Edit shipshape" button is enabled while the editor is installed and not running.
    void SetShipEditor(bool available, bool running) { editor_available_ = available; editor_running_ = running; }
    // True once after the button was pressed. *shape: the ship definition to edit when the
    // choice is not saved yet, else empty (the ship editor then opens the xpilotrc ship).
    bool TakeShipEditorRequest(std::string* shape);

    // Client arguments for every value that was changed but not saved to the
    // xpilotrc (keys, mouse buttons, modifier banks, audio, network, fx), so they
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
    void RenderFx();
    std::vector<std::pair<std::string, std::string>> Changes() const;

    static constexpr int kButtons = 5, kBanks = 4;

    char name_[64] = "";
    std::string stock_path_;
    std::vector<ss::Entry> stock_, mine_;        // lib/shipshapes.cfg and the personal collection
    std::string ship_;                           // chosen ship: full definition, or what the xpilotrc has
    std::string rc_ship_;                        // xpilot.shipShape as loaded (a definition or a ship name)
    ss::ShipPicker ship_picker_;
    bool editor_available_ = false, editor_running_ = false;
    bool edit_request_ = false;
    std::string edit_shape_;

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
        bool jitter_buffer = false;      // maxJitterBuffer > 0
        int jitter_ms = 150;             // upper limit of the buffer delay
        bool mouse_prediction = true;    // dirPrediction
        bool key_turn_mouse = false;     // keyTurnMouse
        int key_turn_rate = 630;         // keyTurnRate, degrees per second
    } net_;
    std::vector<std::pair<std::string, std::string>> NetworkValues() const;

    // visual effects: client options classicGraphics, fx*
    struct Fx {
        bool classic = false;            // classicGraphics: all modern effects off
        bool fullscreen = true;          // fullScreen: start the game in fullscreen
        bool explosions = true;          // fxExplosions
        bool sparks = true;              // fxSparks
        bool mine_explosions = true;     // fxMineExplosions
        bool missile_trails = true;      // fxMissileTrails
        bool laser_glow = true;          // fxLaserGlow
        bool hyperjump = true;           // fxHyperjump
        bool wormholes = true;           // fxWormholes
        bool tether = true;              // fxTether
        bool mines = true;               // fxMines
        bool ball = true;                // fxBall
        bool stars = true;               // fxStars
        bool cannons = true;             // fxCannons
        bool nuke_flash = true;          // fxNukeFlash
        bool screen_shake = true;        // fxScreenShake
        bool contrails = true;           // fxContrails
        bool terrain = true;             // fxTerrain: hill landscape in filled walls
        bool terrain_mesh = true;        // fxTerrainMesh
        bool ship_hull = true;           // fxShipHull: glossy hull, rim-lit outline
        bool ship_halo = true;           // fxShipHalo: team glow in team play
        bool engine_flame = true;        // fxEngineFlame
        bool shield = true;              // fxShield: hexagon shield, hit flashes
        float amount = 1.0f;             // fxAmount, particle multiplier 0..2
    } fx_;
    std::vector<std::pair<std::string, std::string>> FxValues() const;
    bool loaded_ = false;

    std::map<std::string, std::string> rc_;      // values defined in the xpilotrc when loaded
    std::map<std::string, std::string> shown_;   // what the editor showed after loading

    int capture_ = -1;                           // key row waiting for a key press
    bool capture_add_ = false;
    int cancelled_frame_ = -1;
    std::string status_;
    bool status_error_ = false;
};
