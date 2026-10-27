/*
 * XPilot TNG - adaptive playout (jitter) buffer, see playout.c.
 */

#ifndef PLAYOUT_H
#define PLAYOUT_H

void Playout_start(void);
bool Playout_active(void);
int Playout_read(sock_t *sock, char *buf, int max);
bool Playout_due(void);
int Playout_wait_us(int max_us);

#endif
