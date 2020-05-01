#ifndef UDPBD_H
#define UDPBD_H


//#include "tamtypes.h"


#define UDPBD_PORT          0xBDBD //The port on which to listen for incoming data
#define UDPBD_HEADER_MAGIC  0xBDBDBDBD
#define UDPBD_CMD_INFO      0x00
#define UDPBD_CMD_READ      0x01
#define UDPBD_CMD_WRITE     0x02
#define UDPBD_MAX_DATA      1408 // 1408 bytes = max 11 x 128b blocks


struct SUDPBD_Header {      //   18 bytes
    uint16_t ps2_align;     //    2 bytes  needed to align the packet to a 4byte boundary

	uint32_t bd_magic;      //    4 bytes
	uint8_t  bd_cmd;        //    1 byte
	uint8_t  bd_cmdid;      //    1 byte   increment with every request
	uint8_t  bd_cmdpkt;     //    1 bytes  0=request, 1 or more are response packets
	uint8_t  bd_count;      //    1 bytes  sectorCount 0..88
	uint32_t bd_par1;       //    4 bytes
	uint32_t bd_par2;       //    4 bytes
} __attribute__((__packed__));

struct SUDPBD_Packet {
    struct SUDPBD_Header hdr;
    uint8_t data[UDPBD_MAX_DATA];
} __attribute__((__packed__));


#endif
