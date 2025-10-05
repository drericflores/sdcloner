// sdcloner_engine.c
// Safe SD clone engine with: raw imaging, FS-aware imaging-to-fit, and burn.
// License: GPLv3

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fs.h>     // BLKGETSIZE64
#include <dirent.h>
#include <time.h>

#define KB(x) ((uint64_t)(x) * 1024ULL)
#define MB(x) ((uint64_t)(x) * 1024ULL * 1024ULL)
#define GB(x) ((uint64_t)(x) * 1024ULL * 1024ULL * 1024ULL)

#define SAFETY_MARGIN_BYTES MB(512) // extra room for metadata/slack

static void die(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static void logi(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
    fflush(stdout);
}

static int run_cmd(const char* cmd) {
    logi("[CMD] %s", cmd);
    int rc = system(cmd);
    if (rc == -1) return -1;
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static char* run_cmd_capture(const char* cmd) {
    logi("[CMD] %s", cmd);
    FILE* fp = popen(cmd, "r");
    if (!fp) return NULL;
    char* buf = NULL; size_t cap = 0; size_t len = 0;
    char tmp[4096];
    while (fgets(tmp, sizeof(tmp), fp)) {
        size_t add = strlen(tmp);
        if (len + add + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 8192;
            while (ncap < len + add + 1) ncap *= 2;
            buf = (char*)realloc(buf, ncap);
            cap = ncap;
        }
        memcpy(buf + len, tmp, add);
        len += add;
        buf[len] = '\0';
    }
    pclose(fp);
    return buf;
}

static uint64_t get_blockdev_size_bytes(const char* devnode) {
    uint64_t bytes = 0;
    int fd = open(devnode, O_RDONLY | O_CLOEXEC);
    if (fd < 0) die("open(%s): %s", devnode, strerror(errno));
    if (ioctl(fd, BLKGETSIZE64, &bytes) < 0) {
        close(fd);
        die("ioctl(BLKGETSIZE64 %s): %s", devnode, strerror(errno));
    }
    close(fd);
    return bytes;
}

// List partitions for a disk (e.g. /dev/sdd -> /dev/sdd1, /dev/sdd2).
// Returns a malloc'd array of strings; caller frees each and the array.
static char** list_partitions(const char* disk, int* out_count) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "lsblk -rno PATH,TYPE '%s' | awk '$2==\"part\"{print $1}'", disk);
    char* out = run_cmd_capture(cmd);
    if (!out) { *out_count = 0; return NULL; }
    int cap = 8, n = 0;
    char** arr = malloc(sizeof(char*) * cap);
    char* save = NULL;
    for (char* line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;
        if (n == cap) { cap *= 2; arr = realloc(arr, sizeof(char*) * cap); }
        arr[n++] = strdup(line);
    }
    free(out);
    *out_count = n;
    return arr;
}

// Try to get fstype for a partition
static char* get_fstype(const char* part) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "blkid -o value -s TYPE '%s' 2>/dev/null", part);
    char* out = run_cmd_capture(cmd);
    if (!out) return strdup("unknown");
    char* nl = strchr(out, '\n'); if (nl) *nl = '\0';
    if (!*out) { free(out); return strdup("unknown"); }
    return out;
}

// Compute used bytes by mounting RO (if not mounted) and running df.
// Returns sum of used (bytes) for supported filesystems.
static uint64_t compute_used_bytes_sum(const char* disk) {
    int n = 0;
    char** parts = list_partitions(disk, &n);
    if (!parts || n==0) return 0;
    uint64_t sum = 0;

    for (int i=0;i<n;i++) {
        char* fs = get_fstype(parts[i]);
        bool supported = (!strcmp(fs,"ext2") || !strcmp(fs,"ext3") || !strcmp(fs,"ext4") ||
                          !strcmp(fs,"vfat") || !strcmp(fs,"fat") || !strcmp(fs,"fat32") ||
                          !strcmp(fs,"exfat"));
        free(fs);

        // Current mountpoint (if any)
        char cmd_mp[512];
        snprintf(cmd_mp,sizeof(cmd_mp),
                 "lsblk -rno MOUNTPOINT '%s' | head -n1", parts[i]);
        char* mp = run_cmd_capture(cmd_mp);
        if (mp) { char* nl = strchr(mp, '\n'); if (nl) *nl = '\0'; }
        bool temp_mount=false;
        char mnt[128]={0};

        if (supported) {
            if (!mp || strlen(mp)==0) {
                // mount read-only to temp
                snprintf(mnt,sizeof(mnt),"/mnt/sdcloner_src_%d", i);
                char mk[256]; snprintf(mk,sizeof(mk),"sudo mkdir -p '%s'", mnt);
                run_cmd(mk);
                char mcmd[512]; snprintf(mcmd,sizeof(mcmd),
                    "sudo mount -o ro '%s' '%s' 2>/dev/null", parts[i], mnt);
                if (run_cmd(mcmd)==0) { temp_mount=true; }
            } else {
                strncpy(mnt, mp, sizeof(mnt)-1);
            }

            if (mnt[0]) {
                char cmd_df[512];
                snprintf(cmd_df,sizeof(cmd_df),
                    "df --output=used -B1 '%s' | tail -n1", mnt);
                char* used = run_cmd_capture(cmd_df);
                if (used) {
                    uint64_t u = strtoull(used, NULL, 10);
                    sum += u;
                    free(used);
                }
            }

            if (temp_mount) {
                char um[256]; snprintf(um,sizeof(um),"sudo umount '%s'", mnt);
                run_cmd(um);
                char rm[256]; snprintf(rm,sizeof(rm),"sudo rmdir '%s'", mnt);
                run_cmd(rm);
            }
        }
        if (mp) free(mp);
        free(parts[i]);
    }
    free(parts);
    return sum;
}

// Create image directory
static void ensure_image_dir(char* out_dir, size_t cap) {
    const char* home = getenv("HOME"); if (!home) home = "/tmp";
    snprintf(out_dir, cap, "%s/SDCloner/images", home);
    char mk[512]; snprintf(mk,sizeof(mk),"mkdir -p '%s'", out_dir);
    run_cmd(mk);
}

// Create a timestamped path
static void timestamp_path(char* out, size_t cap, const char* dir, const char* ext) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(out, cap, "%s/clone-%04d%02d%02d-%02d%02d%02d.%s",
             dir,
             1900 + tmv.tm_year, 1 + tmv.tm_mon, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
             ext);
}

// RAW image (bit-for-bit) → gzip
static int make_raw_image_gz(const char* src_disk, char* out_path, size_t out_cap) {
    char dir[256]; ensure_image_dir(dir, sizeof(dir));
    timestamp_path(out_path, out_cap, dir, "img.gz");
    char cmd[1024];
    snprintf(cmd,sizeof(cmd),
       "sudo dd if='%s' bs=4M status=progress | gzip > '%s'", src_disk, out_path);
    int rc = run_cmd(cmd);
    return rc;
}

// Filesystem-aware image that fits within target_bytes.
// Minimal implementation: single-partition FAT32 and rsync of first partition.
// Extend to mirror multiple partitions as needed for your device layout.
static int make_fsaware_image_fit(const char* src_disk, uint64_t target_bytes,
                                  char* out_path, size_t out_cap) {
    int n=0; char** parts = list_partitions(src_disk,&n);
    if (n==0) { if (parts) free(parts); die("No partitions found on %s", src_disk); }

    uint64_t used = compute_used_bytes_sum(src_disk);
    uint64_t need = used + SAFETY_MARGIN_BYTES;
    if (need > target_bytes) {
        for (int i=0;i<n;i++) { free(parts[i]); }
        free(parts);
        die("Destination capacity too small: need ~%lu MB, have ~%lu MB",
            (unsigned long)(need/1024/1024), (unsigned long)(target_bytes/1024/1024));
    }

    char dir[256]; ensure_image_dir(dir, sizeof(dir));
    timestamp_path(out_path, out_cap, dir, "img"); // uncompressed file
    char falloc[512]; snprintf(falloc,sizeof(falloc),
        "truncate -s %lu '%s'", (unsigned long)target_bytes, out_path);
    if (run_cmd(falloc)!=0) die("Failed to create image file");

    // Create loop device for image
    char* loop = run_cmd_capture("sudo losetup -f");
    if (!loop) die("losetup -f failed");
    char* nl = strchr(loop,'\n'); if (nl) *nl = '\0';
    char set[512]; snprintf(set,sizeof(set), "sudo losetup -P '%s' '%s'", loop, out_path);
    if (run_cmd(set)!=0) { free(loop); die("losetup -P failed"); }

    // Create msdos label and a single FAT32 partition filling the image.
    char mklabel[512]; snprintf(mklabel,sizeof(mklabel),
        "sudo parted -s '%s' mklabel msdos", loop);
    if (run_cmd(mklabel)!=0) { free(loop); die("parted mklabel failed"); }

    char mkpart[512]; snprintf(mkpart,sizeof(mkpart),
        "sudo parted -s '%s' mkpart primary fat32 1MiB 100%%", loop);
    if (run_cmd(mkpart)!=0) { free(loop); die("parted mkpart failed"); }

    // Get loop p1 node
    char *loop_p1 = NULL;
    {
        char q[512]; snprintf(q,sizeof(q),"ls '%sp1' 2>/dev/null", loop);
        loop_p1 = run_cmd_capture(q);
        if (loop_p1) { nl = strchr(loop_p1,'\n'); if (nl) *nl = '\0'; }
        if (!loop_p1 || !*loop_p1) { free(loop); die("Could not find loop partition node"); }
    }

    // Format target partition FAT32
    char mkfs[512]; snprintf(mkfs,sizeof(mkfs),"sudo mkfs.vfat -F32 -n CLONE '%s'", loop_p1);
    if (run_cmd(mkfs)!=0) { free(loop_p1); free(loop); die("mkfs target failed"); }

    // Mount RO source (first partition) and RW image, then rsync
    char msrc[64], mtgt[64];
    snprintf(msrc,sizeof(msrc),"/mnt/sdcloner_src");
    snprintf(mtgt,sizeof(mtgt),"/mnt/sdcloner_img");
    run_cmd("sudo mkdir -p /mnt/sdcloner_src /mnt/sdcloner_img");

    int srcn=0; char** sp = list_partitions(src_disk, &srcn);
    if (srcn==0) { free(loop_p1); free(loop); die("No source partitions"); }
    char m1[512]; snprintf(m1,sizeof(m1),"sudo mount -o ro '%s' '%s'", sp[0], msrc);
    if (run_cmd(m1)!=0) { free(loop_p1); free(loop); die("mount source failed"); }

    char m2[512]; snprintf(m2,sizeof(m2),"sudo mount '%s' '%s'", loop_p1, mtgt);
    if (run_cmd(m2)!=0) {
        run_cmd("sudo umount /mnt/sdcloner_src");
        free(loop_p1); free(loop); die("mount target failed");
    }

    int rc = run_cmd("sudo rsync -aHAX --numeric-ids /mnt/sdcloner_src/ /mnt/sdcloner_img/");
    run_cmd("sync");
    run_cmd("sudo umount /mnt/sdcloner_img");
    run_cmd("sudo umount /mnt/sdcloner_src");

    for (int i=0;i<srcn;i++) { free(sp[i]); }
    free(sp);

    // Detach loop
    char dt[512]; snprintf(dt,sizeof(dt),"sudo losetup -d '%s'", loop);
    run_cmd(dt);
    free(loop_p1);
    free(loop);
    return rc;
}

// Burn raw .img.gz or .img to destination
int burn_image_to_disk(const char* image_path, const char* dest_disk) {
    // Unmount any partitions
    char um[512]; snprintf(um,sizeof(um),
        "lsblk -rno MOUNTPOINT '%s' | tail -n+2 | xargs -r -n1 sudo umount 2>/dev/null", dest_disk);
    run_cmd(um);

    const char* gz = strstr(image_path,".gz") ? "gzip -dc" : "cat";
    char cmd[1024];
    snprintf(cmd,sizeof(cmd),
        "%s '%s' | sudo dd of='%s' bs=4M status=progress conv=fsync",
        gz, image_path, dest_disk);
    return run_cmd(cmd);
}

// High-level: decide and act
// If dest_disk==NULL → create image locally.
// If dest_disk provided → choose raw vs fs-aware based on capacity vs used.
int sdcloner_clone(const char* src_disk, const char* dest_disk,
                   uint64_t dest_capacity_hint /* 0 if unknown */) {
    if (!src_disk || access(src_disk, R_OK)!=0) die("Source %s not readable", src_disk);

    uint64_t src_bytes = get_blockdev_size_bytes(src_disk);
    logi("Source size: %.2f GB", (double)src_bytes/ (double)GB(1));
    uint64_t used = compute_used_bytes_sum(src_disk);
    logi("Estimated used data: %.2f GB", (double)used/(double)GB(1));

    char outpath[512];

    if (!dest_disk || !*dest_disk) {
        // Save image locally
        logi("No destination present → creating local image");
        if (dest_capacity_hint && dest_capacity_hint < src_bytes) {
            if (used + SAFETY_MARGIN_BYTES <= dest_capacity_hint) {
                logi("Making FS-aware image to fit within %.2f GB", (double)dest_capacity_hint/(double)GB(1));
                return make_fsaware_image_fit(src_disk, dest_capacity_hint, outpath, sizeof(outpath));
            } else {
                die("Future destination too small (need ~%.2f GB incl. margin)",
                    (double)(used+SAFETY_MARGIN_BYTES)/(double)GB(1));
            }
        }
        int rc = make_raw_image_gz(src_disk, outpath, sizeof(outpath));
        if (rc==0) logi("Image ready: %s", outpath);
        return rc;
    } else {
        // Destination provided: check size
        uint64_t dst_bytes = get_blockdev_size_bytes(dest_disk);
        logi("Destination size: %.2f GB", (double)dst_bytes/(double)GB(1));

        if (dst_bytes >= src_bytes) {
            logi("Destination >= source → raw clone (image+burn)");
            int rc1 = make_raw_image_gz(src_disk, outpath, sizeof(outpath));
            if (rc1!=0) return rc1;
            logi("Raw image created: %s", outpath);
            return burn_image_to_disk(outpath, dest_disk);
        } else {
            if (used + SAFETY_MARGIN_BYTES > dst_bytes) {
                die("Destination smaller than used data + margin (need ~%.2f GB)",
                    (double)(used+SAFETY_MARGIN_BYTES)/(double)GB(1));
            }
            logi("Destination smaller, but used fits → FS-aware image");
            int rc2 = make_fsaware_image_fit(src_disk, dst_bytes, outpath, sizeof(outpath));
            if (rc2!=0) return rc2;
            logi("FS-aware image created: %s", outpath);
            return burn_image_to_disk(outpath, dest_disk);
        }
    }
}
