#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "udpbd.h"

#ifdef __APPLE__ & __MACH__
#include <sys/types.h>
#define lseek64 lseek
#define loff_t off_t
#define readahead(f, o, t) {fcntl(f, F_RDAHEAD, 1); read(f, o, t);};
#endif

#define BUFLEN  2048    //Max length of buffer


void die(const char *s)
{
    perror(s);
    exit(1);
}

int main(int argc, char * argv[])
{
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    int fp, s, recv_len;
    char buf[BUFLEN];
    struct SUDPBD_Header * hdr = (struct SUDPBD_Header *)buf;
    bool read_only = false;
    bool read_only_write_error = false;
    off_t fsize;

    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s <file>\n", argv[0]);
        return 1;
    }

    const char * sFile = argv[1];

    // Open the selected file.
    // This file will be used as the Block Device
    fp = open(sFile, read_only ? O_RDONLY : O_RDWR);
    if (fp < 0) {
        read_only = true;

        fp = open(sFile, read_only ? O_RDONLY : O_RDWR);
        if (fp < 0)
            die(sFile);
    }
    // Get the size of the file
    fsize = lseek(fp, 0, SEEK_END);
    lseek(fp, 0, SEEK_SET);

    printf("Opened '%s' as Block Device\n", sFile);
    printf(" - %s\n", read_only ? "read-only" : "read/write");
    printf(" - size = %ldMB / %ldMiB\n", fsize / (1000*1000), fsize / (1024*1024));

    //create a UDP socket
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        close(fp);
        die("socket");
    }

    //bind socket to port
    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(UDPBD_PORT);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr*)&si_me, sizeof(si_me) ) == -1) {
        close(fp);
        die("bind");
    }

    // Enable broadcasts
    int broadcastEnable=1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    // Send INFO broadcast
    hdr->bd_magic  = UDPBD_HEADER_MAGIC;
    hdr->bd_cmd    = UDPBD_CMD_INFO;
    hdr->bd_cmdid  = 0;
    hdr->bd_cmdpkt = 1;
    hdr->bd_count  = 0;
    hdr->bd_par1   = 512;       // Sector size
    hdr->bd_par2   = fsize/512; // Sector count
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(UDPBD_PORT);
    si_other.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    if (sendto(s, buf, sizeof(struct SUDPBD_Header), 0, (struct sockaddr*) &si_other, slen) == -1) {
        close(fp);
        die("sendto");
    }

    printf("Server running on port %d (0x%x)\n", UDPBD_PORT, UDPBD_PORT);

    // Start server loop
    while (1) {
        // Receive command from ps2
        if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == -1) {
            close(fp);
            die("recvfrom");
        }

        // Check header magic
        if (hdr->bd_magic != UDPBD_HEADER_MAGIC) {
            printf("Invalid header magic: 0x%x\n", hdr->bd_magic);
            continue;
        }

        // Check if this packet is a request
        if ((hdr->bd_cmdpkt != 0) && (hdr->bd_cmd != UDPBD_CMD_WRITE)) {
            //printf("Invalid cmdpkt: %d\n", hdr->bd_cmdpkt);
            continue;
        }

        // Process command
        switch (hdr->bd_cmd) {
            case UDPBD_CMD_INFO:
                {
                    struct SUDPBD_Packet * pkt = (struct SUDPBD_Packet *)buf;
                    char str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &si_other.sin_addr, str, INET_ADDRSTRLEN);

                    printf("UDPBD_CMD_INFO from %s\n", str);

                    //pkt->hdr.bd_magic  = UDPBD_HEADER_MAGIC;
                    //pkt->hdr.bd_cmd    = UDPBD_CMD_INFO;
                    //pkt->hdr.bd_cmdid  = pkt->hdr.bd_cmdid;
                    pkt->hdr.bd_cmdpkt = 1;
                    pkt->hdr.bd_count  = 0;
                    pkt->hdr.bd_par1   = 512;       // Sector size
                    pkt->hdr.bd_par2   = fsize/512; // Sector count

                    // Send packet to ps2
                    if (sendto(s, buf, sizeof(struct SUDPBD_Header), 0, (struct sockaddr*) &si_other, slen) == -1) {
                        close(fp);
                        die("sendto");
                    }
                }
                break;
            case UDPBD_CMD_READ:
                {
                    struct SUDPBD_Packet * pkt = (struct SUDPBD_Packet *)buf;
                    uint32_t total_size = pkt->hdr.bd_count * 512;
                    loff_t offset = (loff_t)pkt->hdr.bd_par1 * 512;
                    uint32_t size_left = total_size;

                    printf("UDPBD_CMD_READ(cmdId=%d, startSector=%d, sectorCount=%d)\n", pkt->hdr.bd_cmdid, pkt->hdr.bd_par1, pkt->hdr.bd_count);

                    // lseek to the requested file position
                    lseek64(fp, offset, SEEK_SET);

                    // readahead 2x the requested size
                    readahead(fp, offset, total_size*2);

                    //pkt->hdr.bd_magic  = UDPBD_HEADER_MAGIC;
                    //pkt->hdr.bd_cmd    = UDPBD_CMD_READ;
                    //pkt->hdr.bd_cmdid  = pkt->hdr.bd_cmdid;
                    pkt->hdr.bd_cmdpkt = 1;
                    pkt->hdr.bd_count  = 0;
                    pkt->hdr.bd_par1   = 0;
                    pkt->hdr.bd_par2   = 0; // not used

                    // Packet loop
                    while (size_left > 0) {
                        uint32_t tx_size = (size_left > UDPBD_MAX_DATA) ? UDPBD_MAX_DATA : size_left;
                        size_left -= tx_size;

                        // read data from file
                        if (read(fp, pkt->data, tx_size) != tx_size)
                            printf("ERROR: read failed\n");
                        pkt->hdr.bd_par1 = tx_size;

                        // Send packet to ps2
                        if (sendto(s, buf, sizeof(struct SUDPBD_Header) + tx_size, 0, (struct sockaddr*) &si_other, slen) == -1) {
                            close(fp);
                            die("sendto");
                        }
                        pkt->hdr.bd_cmdpkt++;
                    }
                }
                break;
            case UDPBD_CMD_WRITE:
                if (read_only == true) {
                    if (read_only_write_error == false) {
                        printf("Warning: File is read only! ignoring all write commands\n");
                        printf(" -> future messages will be suppressed\n");
                        read_only_write_error = true;
                    }
                    break;
                }
                else {
                    struct SUDPBD_Packet * pkt = (struct SUDPBD_Packet *)buf;
                    uint32_t total_size = pkt->hdr.bd_count * 512;
                    uint32_t offset = pkt->hdr.bd_par1 * 512;
                    static uint32_t size_left;
                    static uint32_t cmdpkt = 0;

                    printf("UDPBD_CMD_WRITE(cmdId=%d, startSector=%d, sectorCount=%d)\n", pkt->hdr.bd_cmdid, pkt->hdr.bd_par1, pkt->hdr.bd_count);

                    offset += pkt->hdr.bd_cmdpkt * UDPBD_MAX_DATA;

                    // Check packet sequence
                    if (pkt->hdr.bd_cmdpkt != cmdpkt) {
                        printf("ERROR: invalid cmdpkt: %d\n", pkt->hdr.bd_cmdpkt);
                        printf(" -> Possible data corruption!\n");
                        printf(" -> Fallback to read only mode\n");
                        read_only = true;
                        break;
                    }

                    // First packet
                    if (pkt->hdr.bd_cmdpkt == 0) {
                        size_left = total_size;
                        cmdpkt = 0;
                    }

                    uint32_t write_size = size_left < UDPBD_MAX_DATA ? size_left : UDPBD_MAX_DATA;
                    printf("-> Writing %d bytes to %d\n", write_size, offset);

                    // lseek to the requested file position
                    lseek(fp, offset, SEEK_SET);

                    // write data to file
                    //write(fp, pkt->data, write_size);

                    size_left -= write_size;
                    cmdpkt = size_left == 0 ? 0 : cmdpkt+1;
                }
                break;
            default:
                printf("unknown command: 0x%x\n", hdr->bd_cmd);
        };
    }

    close(fp);
    close(s);

    return 0;
}
