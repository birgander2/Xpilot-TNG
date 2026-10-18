# Voice Chat

Players with a headset can talk to each other in game. The server only
forwards the audio (Opus, ~24 kbit/s per speaker); it never decodes it.

## Channels

| Channel | Who hears you | Default sound |
|---|---|---|
| **Team** | Your teammates only | Full volume at any distance, like a radio |
| **All** | Everybody nearby, enemies included | Fades out with distance, panned left/right towards the speaker |

On servers without teams the team key talks on the all channel.

## Talking

- **Push-to-talk** (default): hold `G` (team) or `H` (all) while speaking.
  Keys: `keyVoiceTeam`, `keyVoiceAll`.
- **Voice activation**: `voiceActivation: true` sends whenever the microphone
  is louder than `voiceActivationLevel` dB. The push-to-talk keys still work
  and override the channel.

While you send, the HUD shows `VOICE TEAM` / `VOICE ALL` (bottom right).
The names of players currently speaking are listed above it
(`[T]` = team channel).

Mute a player with `\ignore <name>` (this mutes their messages and voice),
undo with `\unignore <name>`.

## Client options (`~/.xpilotrc` or command line)

| Option | Default | Meaning |
|---|---|---|
| `voiceChat` | `true` | Enable voice chat |
| `voiceActivation` | `false` | Hands-free sending instead of push-to-talk |
| `voiceActivationTeam` | `true` | Voice activation talks on the team channel |
| `voiceActivationLevel` | `-40` | Trigger level in dB (−80 … 0), raise it if noise opens the channel |
| `voiceVolume` | `100` | Playback volume of other voices (0–200 %) |
| `voiceMicGain` | `100` | Microphone amplification (0–400 %) |
| `voiceBitrate` | `24` | Opus bitrate in kbit/s (6–64) |

The microphone is opened the first time you press a push-to-talk key (or
right away with voice activation) and stays open until the client quits.

## Server options

| Option | Default | Meaning |
|---|---|---|
| `voiceChat` | `true` | Allow voice chat on this server |
| `voiceTeamOnly` | `false` | Only the team channel is allowed |
| `voiceRange` | `1500` | Pixels after which the all channel is silent (0 = unlimited, full volume) |
| `voiceTeamRange` | `0` | Same for the team channel (0 = radio, any distance) |

All of them can be changed at runtime; clients are informed immediately.

## Compatibility

Voice uses protocol version `0x4F20` (TNG reserves `0x4F20`–`0x4F2F`;
`0x4F16`–`0x4F1F` are used by XPilot Infinity and never by TNG).

- XPilot NG / Infinity clients on a TNG server play normally, they just don't
  get voice.
- TNG clients on NG / Infinity servers fall back to protocol `0x4F15`,
  without voice.
- Voice needs the polygon protocol: `.xp2` maps, or `.xp` maps with
  `-polygonMode true`. Old-style `.xp` maps run with protocol `0x4501`
  and have no voice.
- Voice packets are not saved in server recordings.

## Protocol

See `src/common/voice.h`. In short: the client opts in with
`PKT_REQUEST_VOICE`, the server answers with `PKT_VOICE_STATE` (reliable) and
from then on forwards `PKT_VOICE` frames of other players inside the normal
frame updates, with a gain and pan byte per listener. Game data always has
priority: voice is appended last and only as far as the frame has room.
