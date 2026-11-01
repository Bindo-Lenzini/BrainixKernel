#include "net.h"

int net_ipv4_packet_view(const uint8_t *frame, uint32_t frame_len,
                         const uint8_t **out_iph, uint32_t *out_ip_len) {
    if (!frame || !out_iph || !out_ip_len || frame_len < 14 + 20 ||
        frame[12] != 0x08 || frame[13] != 0x00) {
        return 0;
    }

    const uint8_t *iph = frame + 14;
    uint32_t header_len = (uint32_t)(iph[0] & 0x0F) * 4;
    uint32_t ip_len = ((uint32_t)iph[2] << 8) | iph[3];
    uint16_t fragment = ((uint16_t)iph[6] << 8) | iph[7];
    if ((iph[0] >> 4) != 4 || header_len < 20 || header_len > 60 ||
        ip_len < header_len || ip_len > frame_len - 14 ||
        (fragment & 0xBFFF) != 0) {
        return 0;
    }

    *out_iph = iph;
    *out_ip_len = ip_len;
    return 1;
}
