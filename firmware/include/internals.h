#ifndef INTERNALS_H
#define INTERNALS_H

#ifndef __linux__
#include "hardware/i2c.h"
#endif

#define version "1.1"

#define low 0
#define high 1

// defines for rp2040/rp2350
#define GP_NULL 255
#define GP00 0
#define GP01 1
#define GP02 2
#define GP03 3
#define GP04 4
#define GP05 5
#define GP06 6
#define GP07 7
#define GP08 8
#define GP09 9
#define GP10 10
#define GP11 11
#define GP12 12
#define GP13 13
#define GP14 14
#define GP15 15
#define GP16 16
#define GP17 17
#define GP18 18
#define GP19 19
#define GP20 20
#define GP21 21
#define GP22 22
#define GP23 23
#define GP24 24
#define GP25 25
#define GP26 26
#define GP27 27
#define GP28 28
#define GP29 29
#define GP30 30
#define GP31 31
// defines only for rp2350 (qfn-80)
#define GP32 32
#define GP33 33
#define GP34 34
#define GP35 35
#define GP36 36
#define GP37 37
#define GP38 38
#define GP39 39
#define GP40 40
#define GP41 41
#define GP42 42
#define GP43 43
#define GP44 44
#define GP45 45
#define GP46 46
#define GP47 47

#define PIN_NULL GP_NULL

#define IMR_RECV      0x04
#define Sn_IMR_RECV   0x04
#define Sn_IR_RECV    0x04
#define SOCKET_DHCP   0

#ifndef ENCODER_PIO_LEGACY
#define ENCODER_PIO_LEGACY 0
#endif
#ifndef ENCODER_PIO_SUBSTEP
#define ENCODER_PIO_SUBSTEP 1
#endif


#endif // INTERNALS_H