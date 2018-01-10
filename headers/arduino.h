#ifndef __DWM1000_H
#define __DWM1000_H

// Macros in order to avoid hard coding
#define STATE_IDLE          0
#define STATE_SCAN          1
#define STATE_PONG          2
#define STATE_ROUNDROBIN    3
#define STATE_POLL          4
#define STATE_POLLACK       5
#define STATE_RANGE         6
#define STATE_RANGEREPORT   7
#define STATE_PENDING_PONG  8

#define FTYPE_PING        0
#define FTYPE_PONG        1
#define FTYPE_POLL        2
#define FTYPE_POLLACK     3
#define FTYPE_RANGE       4
#define FTYPE_RANGEREPORT 5

// type (1), src (2), dst (2), time1 (5), time2 (5), time3 (5): total (20)
#define FRAME_LEN 20

// Parameters are tunable, but anchors and tags must have the same values
#warning "TODO: Temporary values"
#define PONG_TIMEOUT_MS        100
#define POLLACK_TIMEOUT_MS      10
#define RANGE_TIMEOUT_MS        10
#define RANGEREPORT_TIMEOUT_MS  10
#define PENDING_PONG_TIMEOUT_MS 10
#define REPLY_DELAY_MS           3
#define RESET_TIMEOUT_MS      1000

// Wrapper macro for convenience
#define SET_SRC(buf, addr, sz) memcpy(buf + 1, &addr, sz)
#define SET_DST(buf, addr, sz) memcpy(buf + 3, &addr, sz)
#define GET_SRC(buf, addr, sz) memcpy(&addr, buf + 1, sz)
#define DOES_SRC_MATCH(buf, addr, sz) !memcmp(buf + 1, &addr, sz)
#define DOES_DST_MATCH(buf, addr, sz) !memcmp(buf + 3, &addr, sz)

#endif /* __DWM1000_H */
