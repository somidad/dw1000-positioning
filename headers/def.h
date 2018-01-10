#ifndef __DEF_H
#define DEF_H

// All DWM1000 anchors and tags have 2-byte long address
#define ADDR_SIZE 2
// No anchors nor tags have address 0. It is address of 'nothing'
#define ID_NONE 0
// 0xF000 is a flag bit indicating that an addres is tag's
#define FLAG_TAG (1 << 15)

#endif /* DEF_H */
