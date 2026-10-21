/*
 * XPilot TNG: The Next Generation
 * Client voice chat interface.  Network glue lives in netclient.c,
 * capture, codec and playback in the platform layer (sdl3_voice.c).
 */

#ifndef CVOICE_H
#define CVOICE_H

/* netclient.c */
int Send_voice_request(int on);
int Send_voice(unsigned seq, int channel, const unsigned char *data, int len);

/* platform layer */
void Voice_received(int id, unsigned seq, int channel, int gain, int pan,
		    const unsigned char *data, int len);
void Voice_server_state(int flags, int range, int team_range, int bitrate,
			int team_filter, int all_filter);
void Voice_ptt(int channel, bool down);
void Voice_release_all(void);
void Voice_update(void);
void Voice_paint(void);
void Voice_cleanup(void);
void Store_voice_options(void);

#endif
