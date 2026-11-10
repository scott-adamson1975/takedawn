/*
 * simhub.h - telemetry to SimHub over UDP (simhub_udp.c).
 *
 * BO3_SIMHUB_UDP=<port> or <host>:<port> turns it on (nothing is sent, and no thread runs, without it). A thread sends the
 * shared car snapshot (carstate.h) 60 times a second in the format of Forza Motorsport 7's "Data Out" ("car dash",
 * 311 bytes, little-endian), which SimHub reads natively: in SimHub choose the game "Forza Motorsport 7" and set its
 * listening port to the one used here (docs/simhub-telemetry.md).
 */
#ifndef BO3_SIMHUB_H
#define BO3_SIMHUB_H

/* Reads the environment and, when asked for, asks for the car snapshot and starts the sender. Before main.c's
 * drive_log_start (the snapshot's sampler is on only if somebody asked before it). */
void bo3_simhub_start(void);

#endif /* BO3_SIMHUB_H */
