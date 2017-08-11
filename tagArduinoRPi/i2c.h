#ifndef __I2C_H
#define __I2C_H

#define CMD_NONE       0
#define CMD_SCAN       1
#define CMD_DATA_READY 2
#define CMD_TYPE_ID    3
#define CMD_TYPE_DIST  4

#define TYPE_NONE       0
#define TYPE_DATA_READY 1
#define TYPE_ID         2
#define TYPE_DIST       3

#define I2C_NODATA 0b01111011
#define I2C_DATARD 0b10000011

#endif /* __I2C_H */
