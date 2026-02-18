/*
 * qubesdb-config-inject.c - KVM boot-time config injector
 *
 * Writes initial QubesDB entries through a virtio-serial chardev socket
 * before the VM's qubesdb-daemon connects via vchan-socket. This replaces
 * the xenstore-based config injection path used under Xen.
 *
 * Protocol: standard qubesdb wire format (struct qdb_hdr + data) over
 * the virtio-serial Unix socket created by libvirt.
 *
 * Usage: qubesdb-config-inject <vm-name>
 *   Reads the qubesdb entries from the dom0 qubesdb daemon for the given VM
 *   and writes them to the virtio-serial chardev socket.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "../../include/qubesdb.h"

#define VIRTIO_SERIAL_SOCK_PATTERN "/var/run/qubes/qubesdb.%s.sock"
#define MAX_SOCK_PATH 256

struct config_entry {
    char path[QDB_MAX_PATH];
    char data[QDB_MAX_DATA];
    uint32_t data_len;
};

static int connect_virtio_socket(const char *vm_name)
{
    char sock_path[MAX_SOCK_PATH];
    struct sockaddr_un addr;
    int fd;

    snprintf(sock_path, sizeof(sock_path), VIRTIO_SERIAL_SOCK_PATTERN,
             vm_name);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(sock_path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "socket path too long: %s\n", sock_path);
        close(fd);
        return -1;
    }
    memcpy(addr.sun_path, sock_path, strlen(sock_path) + 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect to virtio-serial socket");
        close(fd);
        return -1;
    }

    return fd;
}

static int send_entry(int fd, uint8_t cmd, const char *path,
                      const void *data, uint32_t data_len)
{
    struct qdb_hdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = cmd;
    if (strlen(path) >= QDB_MAX_PATH) {
        fprintf(stderr, "path too long: %s\n", path);
        return -1;
    }
    memcpy(hdr.path, path, strlen(path) + 1);
    hdr.data_len = data_len;

    /* Send header */
    ssize_t written = write(fd, &hdr, sizeof(hdr));
    if (written != sizeof(hdr)) {
        perror("write header");
        return -1;
    }

    /* Send data payload */
    if (data_len > 0 && data) {
        written = write(fd, data, data_len);
        if (written != (ssize_t)data_len) {
            perror("write data");
            return -1;
        }
    }

    return 0;
}

static int send_end_marker(int fd)
{
    /* End-of-sync marker: MULTIREAD response with empty path and data_len=0 */
    struct qdb_hdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = QDB_RESP_MULTIREAD;
    hdr.path[0] = '\0';
    hdr.data_len = 0;

    ssize_t written = write(fd, &hdr, sizeof(hdr));
    if (written != sizeof(hdr)) {
        perror("write end marker");
        return -1;
    }

    return 0;
}

/*
 * Core VM configuration entries that must be injected at boot time.
 * These are the same keys that qubesdb normally populates from dom0.
 * Currently used as documentation; injection reads from config files.
 */
static const char *const known_config_keys[] __attribute__((unused)) = {
    "/qubes-vm-type",
    "/qubes-vm-persistence",
    "/qubes-vm-updateable",
    "/qubes-ip",
    "/qubes-gateway",
    "/qubes-netmask",
    "/qubes-primary-dns",
    "/qubes-secondary-dns",
    "/qubes-timezone",
    "/qubes-debug-mode",
    "/qubes-mac",
    "/qubes-base-template",
};

static int inject_from_file(int fd, const char *config_path)
{
    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        if (errno == ENOENT)
            return 0;
        perror("fopen config");
        return -1;
    }

    char line[QDB_MAX_PATH + QDB_MAX_DATA + 4];
    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        /* Format: /path=value */
        char *eq = strchr(line, '=');
        if (!eq || line[0] != '/')
            continue;

        *eq = '\0';
        const char *path = line;
        const char *value = eq + 1;

        if (send_entry(fd, QDB_CMD_WRITE, path,
                       value, strlen(value)) < 0) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <vm-name> [config-file]\n"
            "\n"
            "Inject initial QubesDB configuration through virtio-serial.\n"
            "\n"
            "  vm-name      Name of the target VM\n"
            "  config-file  Optional file with key=value pairs to inject\n"
            "               (default: /var/lib/qubes/qubesdb/<vm-name>.conf)\n",
            prog);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *vm_name = argv[1];
    char default_config[256];
    const char *config_path;

    if (argc >= 3) {
        config_path = argv[2];
    } else {
        snprintf(default_config, sizeof(default_config),
                 "/var/lib/qubes/qubesdb/%s.conf", vm_name);
        config_path = default_config;
    }

    fprintf(stderr, "qubesdb-config-inject: connecting to %s\n", vm_name);

    int fd = connect_virtio_socket(vm_name);
    if (fd < 0) {
        fprintf(stderr, "Failed to connect to virtio-serial for %s\n",
                vm_name);
        return 1;
    }

    fprintf(stderr, "qubesdb-config-inject: injecting config for %s\n",
            vm_name);

    /* Inject entries from config file */
    if (inject_from_file(fd, config_path) < 0) {
        fprintf(stderr, "Failed to inject config from %s\n", config_path);
        close(fd);
        return 1;
    }

    /* Send end-of-sync marker */
    if (send_end_marker(fd) < 0) {
        fprintf(stderr, "Failed to send end marker\n");
        close(fd);
        return 1;
    }

    fprintf(stderr, "qubesdb-config-inject: done for %s\n", vm_name);
    close(fd);
    return 0;
}
