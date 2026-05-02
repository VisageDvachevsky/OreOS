#include "kernel.h"

#define NE_BASE 0x300
#define NE_DATA (NE_BASE + 0x10)
#define NE_RESET (NE_BASE + 0x1f)

#define NE_CR 0x00
#define NE_PSTART 0x01
#define NE_PSTOP 0x02
#define NE_BNRY 0x03
#define NE_TPSR 0x04
#define NE_TBCR0 0x05
#define NE_TBCR1 0x06
#define NE_ISR 0x07
#define NE_RSAR0 0x08
#define NE_RSAR1 0x09
#define NE_RBCR0 0x0a
#define NE_RBCR1 0x0b
#define NE_RCR 0x0c
#define NE_TCR 0x0d
#define NE_DCR 0x0e
#define NE_IMR 0x0f
#define NE_PAR0 0x01
#define NE_CURR 0x07
#define NE_MAR0 0x08

#define NE_CMD_STOP 0x21
#define NE_CMD_START 0x22
#define NE_CMD_PAGE1 0x62
#define NE_CMD_RREAD 0x0a
#define NE_CMD_RWRITE 0x12

#define NE_ISR_PRX 0x01
#define NE_ISR_PTX 0x02
#define NE_ISR_TXE 0x08
#define NE_ISR_RDC 0x40

#define NE_TX_PAGE 0x40
#define NE_RX_START 0x46
#define NE_RX_STOP 0x80

static uint32_t net_ready;
static uint8_t net_mac[6];

static void io_wait_local(void) {
    outb(0x80, 0);
}

static void ne_write(uint8_t reg, uint8_t value) {
    outb((uint16_t)(NE_BASE + reg), value);
}

static uint8_t ne_read(uint8_t reg) {
    return inb((uint16_t)(NE_BASE + reg));
}

static void ne_select_page0(void) {
    ne_write(NE_CR, NE_CMD_START);
}

static void ne_select_page1(void) {
    ne_write(NE_CR, NE_CMD_PAGE1);
}

static int ne_wait_rdc(void) {
    for (uint32_t i = 0; i < 1000000; ++i) {
        if (ne_read(NE_ISR) & NE_ISR_RDC) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static void ne_remote_read(uint16_t src, void *dst, uint16_t len) {
    uint8_t *out = dst;
    ne_select_page0();
    ne_write(NE_ISR, NE_ISR_RDC);
    ne_write(NE_RBCR0, (uint8_t)(len & 0xff));
    ne_write(NE_RBCR1, (uint8_t)(len >> 8));
    ne_write(NE_RSAR0, (uint8_t)(src & 0xff));
    ne_write(NE_RSAR1, (uint8_t)(src >> 8));
    ne_write(NE_CR, NE_CMD_RREAD);
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = inw(NE_DATA);
        out[i] = (uint8_t)(w & 0xff);
        if (i + 1 < len) out[i + 1] = (uint8_t)(w >> 8);
    }
    (void)ne_wait_rdc();
    ne_select_page0();
}

static void ne_remote_write(uint16_t dst, const void *src, uint16_t len) {
    const uint8_t *in = src;
    ne_select_page0();
    ne_write(NE_ISR, NE_ISR_RDC);
    ne_write(NE_RBCR0, (uint8_t)(len & 0xff));
    ne_write(NE_RBCR1, (uint8_t)(len >> 8));
    ne_write(NE_RSAR0, (uint8_t)(dst & 0xff));
    ne_write(NE_RSAR1, (uint8_t)(dst >> 8));
    ne_write(NE_CR, NE_CMD_RWRITE);
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = in[i];
        if (i + 1 < len) w |= (uint16_t)in[i + 1] << 8;
        outw(NE_DATA, w);
    }
    (void)ne_wait_rdc();
    ne_select_page0();
}

void net_init(void) {
    net_ready = 0;
    uint8_t reset = inb(NE_RESET);
    outb(NE_RESET, reset);
    io_wait_local();
    for (uint32_t i = 0; i < 100000; ++i) {
        if (ne_read(NE_ISR) & 0x80) break;
        __asm__ volatile("pause");
    }

    ne_write(NE_CR, NE_CMD_STOP);
    ne_write(NE_DCR, 0x49);
    ne_write(NE_RBCR0, 0);
    ne_write(NE_RBCR1, 0);
    ne_write(NE_RCR, 0x20);
    ne_write(NE_TCR, 0x02);
    ne_write(NE_PSTART, NE_RX_START);
    ne_write(NE_PSTOP, NE_RX_STOP);
    ne_write(NE_BNRY, NE_RX_START);
    ne_write(NE_ISR, 0xff);
    ne_write(NE_IMR, 0x00);

    uint8_t prom[32];
    ne_remote_read(0, prom, sizeof(prom));
    for (uint32_t i = 0; i < 6; ++i) net_mac[i] = prom[i * 2];
    if ((net_mac[0] == 0 && net_mac[1] == 0 && net_mac[2] == 0) ||
        (net_mac[0] == 0xff && net_mac[1] == 0xff && net_mac[2] == 0xff)) {
        kprintf("net: NE2000 not detected at io 0x300\n");
        return;
    }

    ne_select_page1();
    for (uint32_t i = 0; i < 6; ++i) ne_write((uint8_t)(NE_PAR0 + i), net_mac[i]);
    for (uint32_t i = 0; i < 8; ++i) ne_write((uint8_t)(NE_MAR0 + i), 0xff);
    ne_write(NE_CURR, NE_RX_START + 1);
    ne_select_page0();
    ne_write(NE_RCR, 0x04);
    ne_write(NE_TCR, 0x00);
    ne_write(NE_ISR, 0xff);
    net_ready = 1;
    kprintf("net: NE2000 io 0x300 mac %x:%x:%x:%x:%x:%x static 10.0.2.15 gw 10.0.2.2\n",
            (uint64_t)net_mac[0], (uint64_t)net_mac[1], (uint64_t)net_mac[2],
            (uint64_t)net_mac[3], (uint64_t)net_mac[4], (uint64_t)net_mac[5]);
}

int net_info(OreNetInfo *info) {
    if (!info) return -ORE_EINVAL;
    info->ready = net_ready;
    info->mtu = 1500;
    for (uint32_t i = 0; i < 6; ++i) info->mac[i] = net_mac[i];
    info->reserved[0] = 0;
    info->reserved[1] = 0;
    info->ip_be = 0x0a00020fU;
    info->gateway_be = 0x0a000202U;
    info->dns_be = 0x0a000203U;
    return 0;
}

int64_t net_send_frame(const void *frame, uint64_t len) {
    if (!net_ready || !frame || len < 14 || len > ORE_NET_MAX_FRAME) return -ORE_EINVAL;
    uint16_t tx_len = (uint16_t)(len < 60 ? 60 : len);
    uint8_t tmp[64];
    if (len < 60) {
        const uint8_t *src = frame;
        for (uint32_t i = 0; i < len; ++i) tmp[i] = src[i];
        for (uint32_t i = (uint32_t)len; i < 60; ++i) tmp[i] = 0;
        frame = tmp;
    }
    for (uint32_t i = 0; i < 1000000; ++i) {
        if ((ne_read(NE_CR) & 0x04) == 0) break;
        __asm__ volatile("pause");
    }
    ne_write(NE_ISR, NE_ISR_PTX | NE_ISR_TXE);
    ne_remote_write((uint16_t)(NE_TX_PAGE * 256), frame, tx_len);
    ne_write(NE_TPSR, NE_TX_PAGE);
    ne_write(NE_TBCR0, (uint8_t)(tx_len & 0xff));
    ne_write(NE_TBCR1, (uint8_t)(tx_len >> 8));
    ne_write(NE_CR, NE_CMD_START | 0x04);
    for (uint32_t i = 0; i < 1000000; ++i) {
        uint8_t isr = ne_read(NE_ISR);
        if (isr & NE_ISR_PTX) {
            ne_write(NE_ISR, NE_ISR_PTX);
            return (int64_t)len;
        }
        if (isr & NE_ISR_TXE) {
            ne_write(NE_ISR, NE_ISR_TXE);
            return -ORE_EINVAL;
        }
        __asm__ volatile("pause");
    }
    return (int64_t)len;
}

int64_t net_recv_frame(void *buffer, uint64_t cap) {
    if (!net_ready || !buffer || cap < 64) return -ORE_EINVAL;
    ne_select_page1();
    uint8_t curr = ne_read(NE_CURR);
    ne_select_page0();
    uint8_t bnry = ne_read(NE_BNRY);
    uint8_t page = (uint8_t)(bnry + 1);
    if (page >= NE_RX_STOP) page = NE_RX_START;
    if (page == curr) return 0;

    uint8_t hdr[4];
    ne_remote_read((uint16_t)(page * 256), hdr, sizeof(hdr));
    uint8_t next = hdr[1];
    uint16_t count = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    if (next < NE_RX_START || next >= NE_RX_STOP || count < 4 || count > 1600) {
        ne_write(NE_BNRY, NE_RX_START);
        return -ORE_EINVAL;
    }
    uint16_t frame_len = (uint16_t)(count - 4);
    if (frame_len > cap) frame_len = (uint16_t)cap;
    uint16_t src = (uint16_t)(page * 256 + 4);
    uint16_t first = (uint16_t)(NE_RX_STOP * 256 - src);
    if (first >= frame_len) {
        ne_remote_read(src, buffer, frame_len);
    } else {
        ne_remote_read(src, buffer, first);
        ne_remote_read((uint16_t)(NE_RX_START * 256), (uint8_t *)buffer + first, (uint16_t)(frame_len - first));
    }
    uint8_t new_bnry = (uint8_t)(next - 1);
    if (new_bnry < NE_RX_START) new_bnry = NE_RX_STOP - 1;
    ne_write(NE_BNRY, new_bnry);
    ne_write(NE_ISR, NE_ISR_PRX);
    return frame_len;
}
