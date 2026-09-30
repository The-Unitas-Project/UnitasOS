#include <kern/e1000.h>
#include <kern/log.h>
#include <kern/network.h>
#include <kern/string.h>
#include <kern/timer.h>

/* This polled network path provides DHCP only. It does not provide sockets. */
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_FIXED_SIZE 240
#define DHCP_OPTION_DISCOVER 1
#define DHCP_OPTION_REQUEST 3
#define DHCP_OPTION_OFFER 2
#define DHCP_OPTION_ACK 5
#define DHCP_OPTION_NAK 6
#define DHCP_RETRY_TICKS 400
#define DHCP_RETRY_LIMIT 4
#define DHCP_TICKS_PER_SECOND 100

#define DHCP_SEEN_NETMASK (1u << 0)
#define DHCP_SEEN_GATEWAY (1u << 1)
#define DHCP_SEEN_DNS (1u << 2)
#define DHCP_SEEN_LEASE (1u << 3)
#define DHCP_SEEN_MESSAGE_TYPE (1u << 4)
#define DHCP_SEEN_SERVER (1u << 5)

enum dhcp_state { DHCP_IDLE, DHCP_DISCOVERING, DHCP_REQUESTING, DHCP_BOUND };

static struct network_status interface_status;
static enum dhcp_state dhcp_state;
static uint32_t transaction_id;
static uint32_t server_address;
static uint32_t offered_address;
static uint64_t last_dhcp_send;
static uint64_t lease_expires_tick;
static unsigned dhcp_attempts;

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint16_t internet_checksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0;
    while (length >= 2) {
        sum += read_be16(data);
        data += 2;
        length -= 2;
    }
    if (length) sum += (uint16_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t ipv4_udp_checksum(const uint8_t source[4],
                                  const uint8_t destination[4],
                                  const uint8_t *udp, size_t length) {
    uint32_t sum = read_be16(source) + read_be16(source + 2) +
                   read_be16(destination) + read_be16(destination + 2) +
                   17 + (uint32_t)length;
    while (length >= 2) {
        sum += read_be16(udp);
        udp += 2;
        length -= 2;
    }
    if (length) sum += (uint16_t)udp[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static void clear_ipv4_configuration(void) {
    interface_status.ipv4_ready = false;
    memset(interface_status.address, 0, sizeof(interface_status.address));
    memset(interface_status.netmask, 0, sizeof(interface_status.netmask));
    memset(interface_status.gateway, 0, sizeof(interface_status.gateway));
    memset(interface_status.dns, 0, sizeof(interface_status.dns));
    lease_expires_tick = 0;
}

static bool append_option(uint8_t *options, size_t capacity, size_t *cursor,
                          uint8_t code, const void *value, uint8_t length) {
    if (*cursor > capacity || capacity - *cursor < (size_t)length + 2)
        return false;
    options[(*cursor)++] = code;
    options[(*cursor)++] = length;
    memcpy(options + *cursor, value, length);
    *cursor += length;
    return true;
}

/* Send one DHCP client packet over an IPv4 broadcast Ethernet frame. */
static int send_dhcp(uint8_t message_type) {
    uint8_t frame[14 + 20 + 8 + 576];
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xff, 6);
    memcpy(frame + 6, interface_status.mac, 6);
    write_be16(frame + 12, 0x0800);

    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    write_be16(ip + 2, 20 + 8 + DHCP_FIXED_SIZE + 64);
    write_be16(ip + 4, (uint16_t)transaction_id);
    write_be16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = 17;
    memset(ip + 12, 0, 4);
    memset(ip + 16, 0xff, 4);
    write_be16(ip + 10, internet_checksum(ip, 20));

    uint8_t *udp = ip + 20;
    write_be16(udp, DHCP_CLIENT_PORT);
    write_be16(udp + 2, DHCP_SERVER_PORT);
    write_be16(udp + 4, 8 + DHCP_FIXED_SIZE + 64);
    udp[6] = 0;
    udp[7] = 0;

    uint8_t *bootp = udp + 8;
    bootp[0] = 1;
    bootp[1] = 1;
    bootp[2] = 6;
    write_be32(bootp + 4, transaction_id);
    write_be16(bootp + 10, 0x8000);
    memcpy(bootp + 28, interface_status.mac, 6);
    bootp[236] = 99;
    bootp[237] = 130;
    bootp[238] = 83;
    bootp[239] = 99;

    uint8_t *options = bootp + DHCP_FIXED_SIZE;
    size_t cursor = 0;
    if (!append_option(options, 64, &cursor, 53, &message_type, 1)) return -1;
    static const uint8_t requested_options[] = { 1, 3, 6, 51, 54 };
    if (!append_option(options, 64, &cursor, 55, requested_options,
                       sizeof(requested_options))) return -1;
    uint8_t client_id[7] = { 1 };
    memcpy(client_id + 1, interface_status.mac, 6);
    if (!append_option(options, 64, &cursor, 61, client_id,
                       sizeof(client_id))) return -1;
    if (message_type == DHCP_OPTION_REQUEST) {
        uint8_t requested_ip[4];
        uint8_t server_id[4];
        write_be32(requested_ip, offered_address);
        write_be32(server_id, server_address);
        if (!append_option(options, 64, &cursor, 50, requested_ip, 4) ||
            !append_option(options, 64, &cursor, 54, server_id, 4))
            return -1;
    }
    if (cursor >= 64) return -1;
    options[cursor++] = 255;
    size_t payload_length = DHCP_FIXED_SIZE + cursor;
    size_t udp_length = 8 + payload_length;
    size_t frame_length = 14 + 20 + udp_length;
    write_be16(ip + 2, (uint16_t)(20 + udp_length));
    write_be16(udp + 4, (uint16_t)udp_length);
    write_be16(ip + 10, 0);
    write_be16(ip + 10, internet_checksum(ip, 20));
    return e1000_send(frame, frame_length);
}

static void start_discover(void) {
    uint64_t ticks = timer_ticks();
    transaction_id = 0x554e4954u ^ (uint32_t)ticks;
    for (size_t index = 0; index < sizeof(interface_status.mac); ++index)
        transaction_id = (transaction_id << 5) ^
                        (transaction_id >> 27) ^ interface_status.mac[index];
    if (!transaction_id) transaction_id = 1;
    offered_address = 0;
    server_address = 0;
    dhcp_state = DHCP_DISCOVERING;
    dhcp_attempts = 1;
    last_dhcp_send = ticks;
    if (send_dhcp(DHCP_OPTION_DISCOVER) != 0)
        log_write(LOG_WARN, "DHCP discover send failed\n");
}

static bool parse_dhcp_options(const uint8_t *options, size_t length,
                               uint8_t *message_type, uint32_t *server,
                               uint32_t *lease_seconds,
                               uint8_t netmask[4], uint8_t gateway[4],
                               uint8_t dns[4]) {
    size_t cursor = 0;
    unsigned seen = 0;
    while (cursor < length) {
        uint8_t code = options[cursor++];
        if (code == 0) continue;
        if (code == 255)
            return (seen & (DHCP_SEEN_MESSAGE_TYPE | DHCP_SEEN_SERVER)) ==
                   (DHCP_SEEN_MESSAGE_TYPE | DHCP_SEEN_SERVER);
        if (cursor >= length) return false;
        uint8_t option_length = options[cursor++];
        if (option_length > length - cursor) return false;
        const uint8_t *value = options + cursor;
        switch (code) {
        case 1:
            if ((seen & DHCP_SEEN_NETMASK) || option_length != 4) return false;
            seen |= DHCP_SEEN_NETMASK;
            memcpy(netmask, value, 4);
            break;
        case 3:
            if ((seen & DHCP_SEEN_GATEWAY) || option_length < 4 ||
                option_length % 4) return false;
            seen |= DHCP_SEEN_GATEWAY;
            memcpy(gateway, value, 4);
            break;
        case 6:
            if ((seen & DHCP_SEEN_DNS) || option_length < 4 ||
                option_length % 4) return false;
            seen |= DHCP_SEEN_DNS;
            memcpy(dns, value, 4);
            break;
        case 51:
            if ((seen & DHCP_SEEN_LEASE) || option_length != 4) return false;
            seen |= DHCP_SEEN_LEASE;
            *lease_seconds = read_be32(value);
            break;
        case 53:
            if ((seen & DHCP_SEEN_MESSAGE_TYPE) || option_length != 1)
                return false;
            seen |= DHCP_SEEN_MESSAGE_TYPE;
            *message_type = value[0];
            break;
        case 54:
            if ((seen & DHCP_SEEN_SERVER) || option_length != 4) return false;
            seen |= DHCP_SEEN_SERVER;
            *server = read_be32(value);
            break;
        default:
            break;
        }
        cursor += option_length;
    }
    return false;
}

/* Accept bounded DHCP replies that match the current transaction identifier. */
static void receive_dhcp(const uint8_t *frame, size_t length) {
    if (length < 14 + 20 || read_be16(frame + 12) != 0x0800) return;
    const uint8_t *ip = frame + 14;
    size_t available = length - 14;
    size_t ip_header_length = (ip[0] & 0x0f) * 4u;
    if ((ip[0] >> 4) != 4 || ip_header_length < 20 ||
        ip_header_length > available || ip[9] != 17 ||
        internet_checksum(ip, ip_header_length) != 0) return;
    uint16_t total_length = read_be16(ip + 2);
    if (total_length < ip_header_length + 8 || total_length > available ||
        (read_be16(ip + 6) & 0xbfff)) return;
    const uint8_t *udp = ip + ip_header_length;
    uint16_t udp_length = read_be16(udp + 4);
    if (read_be16(udp) != DHCP_SERVER_PORT ||
        read_be16(udp + 2) != DHCP_CLIENT_PORT ||
        udp_length < 8 + DHCP_FIXED_SIZE ||
        udp_length != total_length - ip_header_length) return;
    uint16_t udp_checksum = read_be16(udp + 6);
    if (udp_checksum && ipv4_udp_checksum(ip + 12, ip + 16, udp,
                                          udp_length) != 0) return;
    const uint8_t *bootp = udp + 8;
    size_t bootp_length = udp_length - 8;
    if (bootp_length < DHCP_FIXED_SIZE || bootp[0] != 2 ||
        bootp[1] != 1 || bootp[2] != 6 ||
        read_be32(bootp + 4) != transaction_id ||
        memcmp(bootp + 28, interface_status.mac, 6) != 0 ||
        read_be32(bootp + 236) != 0x63825363) return;

    uint8_t message_type = 0;
    uint32_t server = 0;
    uint32_t lease_seconds = 0;
    uint8_t netmask[4] = {0};
    uint8_t gateway[4] = {0};
    uint8_t dns[4] = {0};
    if (!parse_dhcp_options(bootp + DHCP_FIXED_SIZE,
                            bootp_length - DHCP_FIXED_SIZE, &message_type,
                            &server, &lease_seconds, netmask, gateway, dns)) return;

    if (message_type == DHCP_OPTION_OFFER &&
        dhcp_state == DHCP_DISCOVERING) {
        offered_address = read_be32(bootp + 16);
        server_address = server;
        if (!offered_address || !server_address || !lease_seconds) return;
        dhcp_state = DHCP_REQUESTING;
        dhcp_attempts = 1;
        last_dhcp_send = timer_ticks();
        if (send_dhcp(DHCP_OPTION_REQUEST) != 0)
            log_write(LOG_WARN, "DHCP request send failed\n");
    } else if (message_type == DHCP_OPTION_ACK &&
               dhcp_state == DHCP_REQUESTING && server == server_address &&
               read_be32(bootp + 16) == offered_address && lease_seconds) {
        memcpy(interface_status.address, bootp + 16, 4);
        memcpy(interface_status.netmask, netmask, 4);
        memcpy(interface_status.gateway, gateway, 4);
        memcpy(interface_status.dns, dns, 4);
        interface_status.ipv4_ready = true;
        uint64_t now = timer_ticks();
        uint64_t lifetime = (uint64_t)lease_seconds *
                            DHCP_TICKS_PER_SECOND;
        lease_expires_tick = now > UINT64_MAX - lifetime ? UINT64_MAX :
                             now + lifetime;
        dhcp_state = DHCP_BOUND;
        log_write(LOG_INFO, "DHCP configured IPv4 address %u.%u.%u.%u\n",
                  interface_status.address[0], interface_status.address[1],
                  interface_status.address[2], interface_status.address[3]);
    } else if (message_type == DHCP_OPTION_NAK &&
               dhcp_state == DHCP_REQUESTING && server == server_address) {
        clear_ipv4_configuration();
        dhcp_state = DHCP_IDLE;
        dhcp_attempts = 0;
    }
}

void network_receive(const void *frame, size_t length) {
    if (!frame || length < 14 || !interface_status.link_ready) return;
    const uint8_t *bytes = frame;
    if (memcmp(bytes, interface_status.mac, 6) != 0 &&
        memcmp(bytes, "\xff\xff\xff\xff\xff\xff", 6) != 0) return;
    receive_dhcp(bytes, length);
}

void network_set_link(const uint8_t mac[6], bool link_ready) {
    if (!mac) return;
    memcpy(interface_status.mac, mac, sizeof(interface_status.mac));
    interface_status.link_ready = link_ready;
    if (!link_ready) {
        clear_ipv4_configuration();
        dhcp_state = DHCP_IDLE;
    }
}

int network_init(void) {
    memset(&interface_status, 0, sizeof(interface_status));
    dhcp_state = DHCP_IDLE;
    if (e1000_init() != 0) {
        log_write(LOG_INFO, "no supported E1000 network device found\n");
        return 0;
    }
    if (interface_status.link_ready) start_discover();
    return 0;
}

void network_poll(void) {
    e1000_poll();
    if (!interface_status.link_ready) return;
    uint64_t ticks = timer_ticks();
    if (dhcp_state == DHCP_BOUND) {
        if (ticks < lease_expires_tick) return;
        clear_ipv4_configuration();
        dhcp_state = DHCP_IDLE;
        dhcp_attempts = 0;
    }
    if (dhcp_state == DHCP_IDLE) {
        start_discover();
        return;
    }
    if (ticks - last_dhcp_send < DHCP_RETRY_TICKS) return;
    if (dhcp_attempts >= DHCP_RETRY_LIMIT) {
        dhcp_state = DHCP_IDLE;
        return;
    }
    ++dhcp_attempts;
    last_dhcp_send = ticks;
    uint8_t message_type = dhcp_state == DHCP_DISCOVERING ?
                           DHCP_OPTION_DISCOVER : DHCP_OPTION_REQUEST;
    if (send_dhcp(message_type) != 0)
        log_write(LOG_WARN, "DHCP retry send failed\n");
}

void network_get_status(struct network_status *status) {
    if (status) *status = interface_status;
}
