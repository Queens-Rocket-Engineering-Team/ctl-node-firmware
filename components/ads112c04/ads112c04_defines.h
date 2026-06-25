#pragma once

#define ADS112C04_I2C_TIMEOUT 25

#define INVALID_GAIN 0xFF
#define INVALID_MUX 0xFF

// ADS112C04 commands
#define RESET 0x06
#define START_SYNC 0x08
#define POWER_DOWN 0x02
#define RDATA 0x10
#define RREG(reg) (0x20 | (reg << 2))
#define WREG(reg) (0x40 | (reg << 2))

// reg0 masks
#define MUX_MASK 0xF0
#define GAIN_MASK 0x0E
#define PGA_BYPASS_MASK 0x01

// reg1 masks
#define DR_MASK 0xE0
#define MODE_MASK 0x10
#define CM_MASK 0x08
#define VREF_MASK 0x06
#define TS_MASK 0x01

// reg2 masks
#define DRDY_MASK 0x80
#define DCNT_MASK 0x40
#define CRC_MASK 0x30
#define BCS_MASK 0x08
#define IDAC_MASK 0x07

// reg3 masks
#define I1MUX_MASK 0xE0
#define I2MUX_MASK 0x1C
