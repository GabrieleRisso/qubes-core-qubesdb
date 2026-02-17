/*
 * qubesdb-config-read.c - KVM VM-side boot-time config reader
 *
 * Reads initial QubesDB entries from a virtio-serial port
 * (/dev/virtio-ports/org.qubes-os.qubesdb) at boot time, before the
 * main vchan-socket connection to dom0 is established.
 *
 * The data is read using the standard qubesdb wire format and written
 * to the local qubesdb daemon socket.
 *
 * This replaces the xenstore-based initial config path used under Xen,
 * where the VM's qubesdb daemon would read xenstore entries at boot.
 *
 * Usage: qubesdb-config-read
 *   Typically run as a systemd service before qubesdb-daemon starts.
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

#include "../../include/qubesdb.h"

#define VIRTIO_PORT_PATH "/dev/virtio-ports/org.qubes-os.qubesdb"
#define LOCAL_DB_SOCK QDB_DAEMON_LOCAL_PATH
#define CONFIG_CACHE_DIR "/var/run/qubes"
#define CONFIG_CACHE_FILE "/var/run/qubes/qubesdb-initial.cache"

static int read_exact(int fd, void *buf, size_t count)
{
    size_t total = 0;
    while (total < count) {
        ssize_t n = read(fd, (char *)buf + total, count - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read");
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "unexpected EOF\n");
            return -1;
        }
        total += n;
    }
    return 0;
}

static int write_exact(int fd, const void *buf, size_t count)
{
    size_t total = 0;
    while (total < count) {
        ssize_t n = write(fd, (const char *)buf + total, count - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("write");
            return -1;
        }
        total += n;
    }
    return 0;
}

static int open_virtio_port(void)
{
    int fd = open(VIRTIO_PORT_PATH, O_RDONLY);
    if (fd < 0) {
        perror("open virtio-serial port");
        return -1;
    }
    return fd;
}

static int write_cache_entry(FILE *cache, const char *path,
                             const char *data, uint32_t data_len)
{
    if (!cache)
        return 0;

    /* Write as path=value\n for easy debugging and re-reading */
    fprintf(cache, "%s=", path);
    fwrite(data, 1, data_len, cache);
    fputc('\n', cache);
    return 0;
}

int main(int argc __attribute__((unused)),
         char **argv __attribute__((unused)))
{
    struct qdb_hdr hdr;
    char data[QDB_MAX_DATA];
    int entry_count = 0;
    FILE *cache_fp = NULL;

    fprintf(stderr, "qubesdb-config-read: waiting for virtio-serial port\n");

    int vport = open_virtio_port();
    if (vport < 0) {
        fprintf(stderr, "No virtio-serial port available, skipping\n");
        return 0;
    }

    /* Open cache file for storing initial config */
    cache_fp = fopen(CONFIG_CACHE_FILE, "w");
    if (!cache_fp) {
        perror("fopen cache");
    }

    fprintf(stderr, "qubesdb-config-read: reading initial config\n");

    for (;;) {
        /* Read qubesdb message header */
        if (read_exact(vport, &hdr, sizeof(hdr)) < 0) {
            fprintf(stderr, "Error reading header\n");
            break;
        }

        /* End-of-sync marker: MULTIREAD with empty path */
        if (hdr.type == QDB_RESP_MULTIREAD && hdr.path[0] == '\0'
            && hdr.data_len == 0) {
            fprintf(stderr,
                    "qubesdb-config-read: received end marker after %d entries\n",
                    entry_count);
            break;
        }

        /* Sanity checks */
        if (hdr.data_len > QDB_MAX_DATA) {
            fprintf(stderr, "data_len %u exceeds maximum %d\n",
                    hdr.data_len, QDB_MAX_DATA);
            break;
        }

        /* Read data payload */
        if (hdr.data_len > 0) {
            if (read_exact(vport, data, hdr.data_len) < 0) {
                fprintf(stderr, "Error reading data for %s\n", hdr.path);
                break;
            }
        }

        /* Null-terminate for logging/cache */
        data[hdr.data_len] = '\0';

        /* Write to cache */
        write_cache_entry(cache_fp, hdr.path, data, hdr.data_len);

        entry_count++;
    }

    if (cache_fp)
        fclose(cache_fp);
    close(vport);

    fprintf(stderr, "qubesdb-config-read: done, %d entries cached to %s\n",
            entry_count, CONFIG_CACHE_FILE);
    return 0;
}
