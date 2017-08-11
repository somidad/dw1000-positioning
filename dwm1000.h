#ifndef __DWM1000_H
#define __DWM1000_H

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

#define FRAME_LEN 20

#warning "TODO: Temporary values"
#define PONG_TIMEOUT_MS        100
#define POLLACK_TIMEOUT_MS      10
#define RANGE_TIMEOUT_MS        10
#define RANGEREPORT_TIMEOUT_MS  10
#define REPLY_DELAY_MS           3

#endif /* __DWM1000_H */
