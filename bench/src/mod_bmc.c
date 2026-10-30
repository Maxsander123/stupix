/*
 * BMC: in-band IPMI through the Linux IPMI device (/dev/ipmi0), no ipmitool.
 * Identity, self test, chassis faults, SEL fill level, LAN address,
 * DCMI power reading and BMC round-trip latency.
 */
#include "stx.h"

#include <stdlib.h>
#include <string.h>

/* no reference BMC on a laptop: 10 ms per KCS round trip is a typical server value */
#define REF_BMC_LATENCY 10.0 /* ms */

#ifdef STX_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>

/* uapi linux/ipmi.h, replicated to stay header independent */
struct stx_ipmi_msg {
    unsigned char netfn, cmd;
    unsigned short data_len;
    unsigned char *data;
};
struct stx_ipmi_req {
    unsigned char *addr;
    unsigned int addr_len;
    long msgid;
    struct stx_ipmi_msg msg;
};
struct stx_ipmi_recv {
    int recv_type;
    unsigned char *addr;
    unsigned int addr_len;
    long msgid;
    struct stx_ipmi_msg msg;
};
struct stx_ipmi_sys_addr {
    int addr_type;
    short channel;
    unsigned char lun;
};
struct stx_ipmi_addr {
    int addr_type;
    short channel;
    char data[32];
};
#define STX_IPMICTL_RECEIVE_MSG_TRUNC _IOWR('i', 11, struct stx_ipmi_recv)
#define STX_IPMICTL_SEND_COMMAND _IOR('i', 13, struct stx_ipmi_req)

static long msgid;

/* returns completion code (0 = ok) or -1; resp[0] is the completion code */
static int ipmi_cmd(int fd, uint8_t netfn, uint8_t cmd, const uint8_t *req, int reqlen, uint8_t *resp, int *resplen)
{
    struct stx_ipmi_sys_addr a;
    struct stx_ipmi_req r;
    struct stx_ipmi_recv rv;
    struct stx_ipmi_addr ra;
    struct pollfd pfd;
    uint8_t reqbuf[32];
    int cap = *resplen;

    memset(&a, 0, sizeof(a));
    a.addr_type = 0x0c; /* system interface */
    a.channel = 0x0f;   /* BMC */
    memset(&r, 0, sizeof(r));
    if (reqlen > (int)sizeof(reqbuf))
        return -1;
    if (reqlen)
        memcpy(reqbuf, req, (size_t)reqlen);
    r.addr = (unsigned char *)&a;
    r.addr_len = sizeof(a);
    r.msgid = ++msgid;
    r.msg.netfn = netfn;
    r.msg.cmd = cmd;
    r.msg.data_len = (unsigned short)reqlen;
    r.msg.data = reqbuf;
    if (ioctl(fd, STX_IPMICTL_SEND_COMMAND, &r) < 0)
        return -1;
    for (;;) {
        pfd.fd = fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 5000) <= 0)
            return -1;
        memset(&rv, 0, sizeof(rv));
        rv.addr = (unsigned char *)&ra;
        rv.addr_len = sizeof(ra);
        rv.msg.data = resp;
        rv.msg.data_len = (unsigned short)cap;
        if (ioctl(fd, STX_IPMICTL_RECEIVE_MSG_TRUNC, &rv) < 0 && errno != EMSGSIZE)
            return -1;
        if (rv.msgid == msgid)
            break;
    }
    *resplen = rv.msg.data_len;
    return *resplen > 0 ? resp[0] : -1;
}

static const char *ipmi_vendor(unsigned id)
{
    switch (id) {
    case 2: return "IBM";
    case 11: return "HPE";
    case 343: return "Intel";
    case 674: return "Dell";
    case 7244: return "Quanta";
    case 10368: return "Fujitsu";
    case 10876: return "Supermicro";
    case 19046: return "Lenovo";
    default: return "vendor";
    }
}

static int ipmi_open(void)
{
    static const char *devs[] = {"/dev/ipmi0", "/dev/ipmi/0", "/dev/ipmidev/0", NULL};
    int i, fd, attempt;
    for (attempt = 0; attempt < 2; attempt++) {
        for (i = 0; devs[i]; i++) {
            fd = open(devs[i], O_RDWR);
            if (fd >= 0)
                return fd;
        }
        /* SMBIOS type 38 announces a BMC: load the in-band drivers (part of every kernel) */
        if (attempt == 0 && stx_is_root() && stx_file_exists("/sys/firmware/dmi/entries/38-0/raw")) {
            stx_progress("loading ipmi kernel modules");
            if (system("modprobe ipmi_devintf >/dev/null 2>&1; modprobe ipmi_si >/dev/null 2>&1") == 0)
                stx_sleep_ms(1500);
            continue;
        }
        break;
    }
    return -1;
}

static void lan_report(int fd)
{
    uint8_t ch;
    for (ch = 1; ch <= 8; ch++) {
        uint8_t req[4] = {0, 3, 0, 0}, resp[32];
        int len = sizeof(resp);
        req[0] = ch;
        if (ipmi_cmd(fd, 0x0c, 0x02, req, 4, resp, &len) == 0 && len >= 6) {
            char line[128];
            int l = snprintf(line, sizeof(line), "%u.%u.%u.%u", resp[2], resp[3], resp[4], resp[5]);
            req[1] = 4; /* ip source */
            len = sizeof(resp);
            if (ipmi_cmd(fd, 0x0c, 0x02, req, 4, resp, &len) == 0 && len >= 3)
                l += snprintf(line + l, sizeof(line) - (size_t)l, " (%s)",
                              (resp[2] & 0xf) == 1 ? "static" : (resp[2] & 0xf) == 2 ? "dhcp" : "other");
            req[1] = 5; /* mac */
            len = sizeof(resp);
            if (ipmi_cmd(fd, 0x0c, 0x02, req, 4, resp, &len) == 0 && len >= 8)
                snprintf(line + l, sizeof(line) - (size_t)l, ", mac %02x:%02x:%02x:%02x:%02x:%02x", resp[2],
                         resp[3], resp[4], resp[5], resp[6], resp[7]);
            {
                char key[32];
                snprintf(key, sizeof(key), "bmc lan channel %u", ch);
                stx_info(key, "%s", line);
            }
            return;
        }
    }
}

void stx_mod_bmc(void)
{
    uint8_t resp[64];
    int fd, len, i, ok = 0;
    double t;

    stx_section("bmc", "BMC / IPMI");
    if (!stx_is_root()) {
        stx_skip("in-band IPMI needs root");
        return;
    }
    fd = ipmi_open();
    if (fd < 0) {
        if (stx_file_exists("/sys/firmware/dmi/entries/38-0/raw"))
            stx_check(STX_WARN, "bmc", "SMBIOS announces a BMC but the IPMI interface does not answer");
        else
            stx_skip("no BMC present (no IPMI device, no SMBIOS type 38)");
        return;
    }

    len = sizeof(resp);
    if (ipmi_cmd(fd, 0x06, 0x01, NULL, 0, resp, &len) == 0 && len >= 12) {
        unsigned mfg = resp[7] | resp[8] << 8 | (resp[9] & 0x0f) << 16;
        stx_info("bmc", "%s (manufacturer id %u), product 0x%04x", ipmi_vendor(mfg), mfg, resp[10] | resp[11] << 8);
        stx_info("bmc firmware", "%u.%02x, IPMI %u.%u", resp[3] & 0x7f, resp[4], resp[5] & 0xf, resp[5] >> 4);
        if (resp[3] & 0x80)
            stx_check(STX_WARN, "bmc", "device reports firmware update or self-initialisation in progress");
    } else {
        stx_check(STX_FAIL, "bmc", "Get Device ID failed");
        close(fd);
        return;
    }

    len = sizeof(resp);
    if (ipmi_cmd(fd, 0x06, 0x04, NULL, 0, resp, &len) == 0 && len >= 3) {
        if (resp[1] == 0x55)
            stx_check(STX_PASS, "bmc self test", "no error");
        else if (resp[1] == 0x56)
            stx_info("bmc self test", "not implemented");
        else if (resp[1] == 0x58)
            stx_check(STX_FAIL, "bmc self test", "fatal hardware error (0x%02x)", resp[2]);
        else
            stx_check(STX_WARN, "bmc self test", "result 0x%02x 0x%02x", resp[1], resp[2]);
    }

    len = sizeof(resp);
    if (ipmi_cmd(fd, 0x00, 0x01, NULL, 0, resp, &len) == 0 && len >= 4) {
        uint8_t pw = resp[1], misc = resp[3];
        stx_info("chassis power", "%s", pw & 1 ? "on" : "off");
        if (pw & 0x1e)
            stx_check(STX_FAIL, "chassis", "power fault flags 0x%02x (overload/interlock/fault)", pw & 0x1e);
        if (misc & 0x0d)
            stx_check(STX_WARN, "chassis", "%s%s%s", misc & 1 ? "intrusion " : "", misc & 4 ? "drive-fault " : "",
                      misc & 8 ? "fan/cooling-fault" : "");
        if (!(pw & 0x1e) && !(misc & 0x0d))
            stx_check(STX_PASS, "chassis", "no power, cooling or drive faults");
    }

    len = sizeof(resp);
    if (ipmi_cmd(fd, 0x0a, 0x40, NULL, 0, resp, &len) == 0 && len >= 6) {
        unsigned entries = resp[2] | resp[3] << 8, freeb = resp[4] | resp[5] << 8;
        double fill = entries ? 100.0 * entries * 16 / (entries * 16.0 + freeb) : 0;
        stx_info("sel", "%u entries, %.0f %% full", entries, fill);
        if (fill >= 90)
            stx_check(STX_WARN, "sel", "event log %.0f %% full, new events may be lost", fill);
    }
    len = sizeof(resp);
    if (ipmi_cmd(fd, 0x0a, 0x20, NULL, 0, resp, &len) == 0 && len >= 4)
        stx_info("sdr records", "%u", resp[2] | resp[3] << 8);

    lan_report(fd);

    {
        uint8_t req[4] = {0xdc, 0x01, 0x00, 0x00};
        len = sizeof(resp);
        if (ipmi_cmd(fd, 0x2c, 0x02, req, 4, resp, &len) == 0 && len >= 10 && resp[1] == 0xdc) {
            unsigned cur = resp[2] | resp[3] << 8, mn = resp[4] | resp[5] << 8, mx = resp[6] | resp[7] << 8,
                     avg = resp[8] | resp[9] << 8;
            stx_metric("system power (dcmi)", cur, "W", 0, STX_LOWER);
            stx_info("dcmi min/max/avg", "%u / %u / %u W", mn, mx, avg);
            stx_bmc_power_watts = cur;
        }
    }

    stx_progress("bmc latency");
    t = stx_now();
    for (i = 0; i < 20; i++) {
        len = sizeof(resp);
        if (ipmi_cmd(fd, 0x06, 0x01, NULL, 0, resp, &len) == 0)
            ok++;
    }
    t = stx_now() - t;
    if (ok)
        stx_metric("bmc round trip", t * 1000.0 / ok, "ms", REF_BMC_LATENCY, STX_LOWER);
    if (ok < 20)
        stx_check(STX_WARN, "bmc", "%d of 20 requests failed", 20 - ok);
    close(fd);
}

/* used by the load module to sample wall power while the CPU is busy */
double stx_bmc_read_power(void);
double stx_bmc_read_power(void)
{
    static int fd = -2;
    uint8_t req[4] = {0xdc, 0x01, 0x00, 0x00}, resp[32];
    int len = sizeof(resp);
    if (fd == -2) {
        static const char *devs[] = {"/dev/ipmi0", "/dev/ipmi/0", "/dev/ipmidev/0", NULL};
        int i;
        fd = -1;
        for (i = 0; devs[i] && fd < 0; i++)
            fd = open(devs[i], O_RDWR);
    }
    if (fd < 0)
        return -1;
    if (ipmi_cmd(fd, 0x2c, 0x02, req, 4, resp, &len) == 0 && len >= 4 && resp[1] == 0xdc)
        return resp[2] | resp[3] << 8;
    return -1;
}

#else

void stx_mod_bmc(void)
{
    stx_section("bmc", "BMC / IPMI");
    stx_skip("in-band IPMI is implemented for Linux");
}

double stx_bmc_read_power(void);
double stx_bmc_read_power(void)
{
    return -1;
}

#endif
