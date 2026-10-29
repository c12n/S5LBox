/*
 * Firmware-backed tests for the iOS 4.2.1 (8C148) external-memory-disk
 * kernel compatibility gate and its bring-up adapter.
 *
 * Public CI has no Apple firmware, so every case here skips unless
 * S5LBOX_TEST_FIRMWARE_DIR_IOS4 names a directory holding the decrypted
 * 8C148 kernel.macho (as produced by fwimport). When the 3.1.3 firmware
 * directory is also provided (S5LBOX_TEST_FIRMWARE_DIR), the two gates
 * are cross-checked: each must reject the other build's kernel.
 */
#include "ios4_kernel_patch.h"
#include "ios3_kernel_patch.h"
#include "ios4_bringup_gate.h"
#include "macho.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned g_passes;
static unsigned g_failures;
static unsigned g_skips;

#define CHECK(condition, ...) do {                                           \
    if (condition) {                                                         \
        g_passes++;                                                          \
    } else {                                                                 \
        g_failures++;                                                        \
        printf("  FAIL %s:%d: ", __func__, __LINE__);                        \
        printf(__VA_ARGS__);                                                 \
        printf("\n");                                                        \
    }                                                                        \
} while (0)

#define SKIP(...) do {                                                       \
    g_skips++;                                                               \
    printf("  SKIP %s: ", __func__);                                         \
    printf(__VA_ARGS__);                                                     \
    printf("\n");                                                            \
    return;                                                                  \
} while (0)

static uint8_t *slurp(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *size_out = (size_t)n;
    return buf;
}

static char g_kernel_path[1024];
static char g_tree_path[1024];

static const char *firmware_kernel_path(const char *env_name) {
    const char *dir = getenv(env_name);
    if (!dir || !*dir) return NULL;
    snprintf(g_kernel_path, sizeof g_kernel_path, "%s/kernel.macho", dir);
    return g_kernel_path;
}

static const char *firmware_tree_path(const char *env_name) {
    const char *dir = getenv(env_name);
    if (!dir || !*dir) return NULL;
    snprintf(g_tree_path, sizeof g_tree_path, "%s/devicetree.bin", dir);
    return g_tree_path;
}

/* Load the kernel's segments into a fresh RAM image the way the boot
 * loaders do: file bytes at (vmaddr - virt_base), zero fill to vmsize,
 * vmsize-0 segments skipped. Returns NULL on any inconsistency. */
static uint8_t *load_ram(const uint8_t *kernel, size_t kernel_size,
                         uint32_t virt_base, size_t ram_size) {
    macho_t m;
    if (macho_parse(kernel, kernel_size, &m) != MACHO_OK) return NULL;
    uint8_t *ram = (uint8_t *)calloc(1, ram_size);
    if (!ram) return NULL;
    for (size_t i = 0; i < m.segment_count; i++) {
        const macho_segment_t *s = &m.segments[i];
        if (s->vmsize == 0) continue;
        size_t off = (size_t)(s->vmaddr - virt_base);
        if (off + s->filesize > ram_size ||
            (uint64_t)s->fileoff + s->filesize > kernel_size) {
            free(ram);
            return NULL;
        }
        memcpy(ram + off, kernel + s->fileoff, s->filesize);
    }
    return ram;
}

static void test_real_kernel_accepted(void) {
    const char *path = firmware_kernel_path("S5LBOX_TEST_FIRMWARE_DIR_IOS4");
    if (!path) SKIP("S5LBOX_TEST_FIRMWARE_DIR_IOS4 not set");
    size_t size = 0;
    uint8_t *kernel = slurp(path, &size);
    if (!kernel) SKIP("cannot read %s", path);
    CHECK(size == (size_t)IOS4_KERNEL_PATCH_FILE_SIZE,
          "kernel size %zu", size);

    const size_t ram_size = (size_t)128 << 20;
    uint8_t *ram = load_ram(kernel, size, IOS4_KERNEL_PATCH_VIRT_BASE,
                            ram_size);
    CHECK(ram != NULL, "RAM image loads");
    if (!ram) { free(kernel); return; }

    for (int extended = 0; extended <= 1; extended++) {
        /* Reload: the first apply patches the image in place. */
        uint8_t *fresh = load_ram(kernel, size,
                                  IOS4_KERNEL_PATCH_VIRT_BASE, ram_size);
        ios4_kernel_patch_request_t req;
        ios4_kernel_patch_report_t rep;
        memset(&req, 0, sizeof req);
        memset(&rep, 0, sizeof rep);
        req.kernel_file = kernel;
        req.kernel_file_size = size;
        req.ram = fresh;
        req.ram_size = ram_size;
        req.ram_base = IOS4_KERNEL_PATCH_RAM_BASE;
        req.virt_base = IOS4_KERNEL_PATCH_VIRT_BASE;
        req.extended_md_geometry = extended != 0;
        ios4_kernel_patch_status_t st = ios4_kernel_patch_apply(&req, &rep);
        CHECK(st == IOS4_KERNEL_PATCH_STATUS_OK,
              "extended=%d gate: %s (site %s va 0x%llx)", extended,
              ios4_kernel_patch_status_string(st),
              ios4_kernel_patch_site_string(rep.site),
              (unsigned long long)rep.virtual_address);
        if (st == IOS4_KERNEL_PATCH_STATUS_OK) {
            size_t off = (size_t)(IOS4_KERNEL_PATCH_MD_READ_VA -
                                  IOS4_KERNEL_PATCH_VIRT_BASE);
            CHECK(fresh[off] == 0xe1 && fresh[off + 1] == 0xdf,
                  "extended=%d: MD_READ site carries svc 0xe1", extended);
        }
        free(fresh);
    }
    free(ram);
    free(kernel);
}

static void test_digest_rejection(void) {
    const char *path = firmware_kernel_path("S5LBOX_TEST_FIRMWARE_DIR_IOS4");
    if (!path) SKIP("S5LBOX_TEST_FIRMWARE_DIR_IOS4 not set");
    size_t size = 0;
    uint8_t *kernel = slurp(path, &size);
    if (!kernel) SKIP("cannot read %s", path);
    const size_t ram_size = (size_t)128 << 20;
    uint8_t *ram = load_ram(kernel, size, IOS4_KERNEL_PATCH_VIRT_BASE,
                            ram_size);
    if (!ram) { free(kernel); SKIP("RAM image does not load"); }

    kernel[size / 2] ^= 0xffu; /* any single-bit-class change */
    ios4_kernel_patch_request_t req;
    ios4_kernel_patch_report_t rep;
    memset(&req, 0, sizeof req);
    memset(&rep, 0, sizeof rep);
    req.kernel_file = kernel;
    req.kernel_file_size = size;
    req.ram = ram;
    req.ram_size = ram_size;
    req.ram_base = IOS4_KERNEL_PATCH_RAM_BASE;
    req.virt_base = IOS4_KERNEL_PATCH_VIRT_BASE;
    ios4_kernel_patch_status_t st = ios4_kernel_patch_apply(&req, &rep);
    CHECK(st == IOS4_KERNEL_PATCH_STATUS_KERNEL_DIGEST_MISMATCH ||
          st == IOS4_KERNEL_PATCH_STATUS_LOADED_SEGMENT_MISMATCH,
          "mutated kernel rejected, got %s",
          ios4_kernel_patch_status_string(st));
    free(ram);
    free(kernel);
}

static void test_cross_build_rejection(void) {
    const char *path4 = firmware_kernel_path("S5LBOX_TEST_FIRMWARE_DIR_IOS4");
    if (!path4) SKIP("S5LBOX_TEST_FIRMWARE_DIR_IOS4 not set");
    size_t size4 = 0;
    uint8_t *k4 = slurp(path4, &size4);
    if (!k4) SKIP("cannot read %s", path4);
    const size_t ram_size = (size_t)128 << 20;
    uint8_t *ram = load_ram(k4, size4, IOS4_KERNEL_PATCH_VIRT_BASE, ram_size);
    if (!ram) { free(k4); SKIP("RAM image does not load"); }

    /* The 7E18 gate must refuse the 8C148 kernel (size first). */
    ios3_kernel_patch_request_t req3;
    ios3_kernel_patch_report_t rep3;
    memset(&req3, 0, sizeof req3);
    memset(&rep3, 0, sizeof rep3);
    req3.kernel_file = k4;
    req3.kernel_file_size = size4;
    req3.ram = ram;
    req3.ram_size = ram_size;
    req3.ram_base = IOS3_KERNEL_PATCH_RAM_BASE;
    req3.virt_base = IOS3_KERNEL_PATCH_VIRT_BASE;
    CHECK(ios3_kernel_patch_apply(&req3, &rep3) !=
          IOS3_KERNEL_PATCH_STATUS_OK,
          "7E18 gate rejects the 8C148 kernel");
    free(ram);
    free(k4);
}

static void test_bringup_configure(void) {
    s5l_bringup_request_t req;
    memset(&req, 0, sizeof req);
    ios4_bringup_gate_configure(&req, NULL);
    CHECK(req.kernel_gate == ios4_bringup_gate, "base gate installed");
    CHECK(req.md_read_site_pc == IOS4_KERNEL_PATCH_MD_READ_VA, "read site");
    CHECK(req.md_write_site_pc == IOS4_KERNEL_PATCH_MD_WRITE_VA,
          "write site");
    CHECK(req.md_raw_site_pc == IOS4_KERNEL_PATCH_RAW_WATCHER_VA,
          "raw site");
    CHECK(req.uiomove_pc == IOS4_KERNEL_UIOMOVE_VA, "uiomove pc");
    CHECK(req.root_dt_address_override == UINT32_C(0x68000000),
          "RAMDisk word pre-translated for xnu-1504 ptovirt");
    CHECK(req.md_major_va == UINT32_C(0x80257318),
          "raw bridge checks _mdevCMajor at its 8C148 address");
    CHECK(req.md_geometry_sites.register_pc == 0,
          "no geometry sites without a large root medium");
    CHECK(req.packet_sites.rx_pc == 0 && req.packet_sites.tx_pc == 0,
          "packet sites stay zero for 8C148");
}

/* A synthetic root medium, correctly sized and backed by nothing, in the
 * shape of test_bringup.c's: bring-up reads no byte of the medium, it
 * publishes the size and arms the bridges. */
static vm_block_io_status_t sparse_read(void *ctx, uint64_t offset,
                                        void *dst, size_t requested,
                                        size_t *actual) {
    (void)ctx; (void)offset;
    memset(dst, 0, requested);
    *actual = requested;
    return VM_BLOCK_IO_OK;
}

static vm_block_io_status_t sparse_write(void *ctx, uint64_t offset,
                                         const void *src, size_t requested,
                                         size_t *actual) {
    (void)ctx; (void)offset; (void)src; (void)requested;
    *actual = 0;
    return VM_BLOCK_IO_ERROR;   /* never reached during bring-up */
}

static vm_block_io_status_t sparse_flush(void *ctx) {
    (void)ctx;
    return VM_BLOCK_IO_OK;
}

static vm_block_t sparse_media(uint64_t size) {
    vm_block_t block;
    memset(&block, 0, sizeof block);
    block.size = size;
    block.read_at = sparse_read;
    block.write_at = sparse_write;
    block.flush = sparse_flush;
    return block;
}

/* The full core bring-up the app performs for this build: the request
 * carries the 8C148 virtual base (the core's zero default is the 3.1.3
 * base), the gate adapter binds by exact identity, and the machine must
 * come up pointed at the 8C148 entry with the md bridge installed. Run
 * at the real grown work-image size (rootfs + 32 MB, page-rounded --
 * under the geometry bootstrap, so the base gate binds, exactly as in
 * the app) and again above the bootstrap size, where the extended gate
 * and its geometry sites must bind instead. */
static void test_full_bringup(void) {
    const char *kpath = firmware_kernel_path("S5LBOX_TEST_FIRMWARE_DIR_IOS4");
    if (!kpath) SKIP("S5LBOX_TEST_FIRMWARE_DIR_IOS4 not set");
    const char *tpath = firmware_tree_path("S5LBOX_TEST_FIRMWARE_DIR_IOS4");
    size_t ksize = 0, tsize = 0;
    uint8_t *kernel = slurp(kpath, &ksize);
    if (!kernel) SKIP("cannot read %s", kpath);
    uint8_t *tree = slurp(tpath, &tsize);
    if (!tree) { free(kernel); SKIP("cannot read %s", tpath); }

    static const uint64_t sizes[2] = {
        UINT64_C(715055104),            /* the real grown work image */
        UINT64_C(3) << 30,              /* past the geometry bootstrap */
    };
    for (int round = 0; round < 2; round++) {
        s5l8900_t machine;
        s5l_bringup_request_t request;
        s5l_bringup_result_t result;
        ios4_bringup_gate_report_t gate;
        vm_block_t media = sparse_media(sizes[round]);
        s5l_bringup_md_t *md =
            (s5l_bringup_md_t *)calloc(1, sizeof *md);
        CHECK(md != NULL, "could not allocate bridge storage");
        if (!md) { free(tree); free(kernel); return; }
        memset(&gate, 0, sizeof gate);
        CHECK(s5l8900_init(&machine, S5L_BRINGUP_PHYS_BASE,
                           S5L_BRINGUP_RAM_SIZE), "s5l8900_init failed");

        memset(&request, 0, sizeof request);
        request.kernel = kernel;
        request.kernel_size = ksize;
        request.devicetree = tree;
        request.devicetree_size = tsize;
        request.root_media = &media;
        request.virt_base = IOS4_KERNEL_PATCH_VIRT_BASE;
        ios4_bringup_gate_configure_with_policy(&request, &gate, false);
        CHECK(request.kernel_gate != NULL, "gate installed, round %d", round);
        if (round == 0)
            CHECK(request.md_geometry_sites.register_pc == 0,
                  "base gate at the real work-image size");
        else
            CHECK(request.md_geometry_sites.register_pc ==
                  IOS4_KERNEL_PATCH_MD_REGISTER_VA,
                  "extended gate past the geometry bootstrap");

        s5l_bringup_status_t status =
            s5l_bringup(&machine, &request, md, &result);
        CHECK(status == S5L_BRINGUP_OK,
              "round %d bring-up refused: %s at %s (%s)", round,
              s5l_bringup_status_name(status),
              s5l_bringup_stage_name(result.stage), result.detail);
        if (status == S5L_BRINGUP_OK) {
            CHECK(result.virt_base == UINT32_C(0x80000000),
                  "round %d virt base 0x%lx", round,
                  (unsigned long)result.virt_base);
            CHECK(result.entry_va == UINT32_C(0x80067050),
                  "round %d entry va 0x%lx", round,
                  (unsigned long)result.entry_va);
            CHECK(result.entry_pa == UINT32_C(0x08067050),
                  "round %d entry pa 0x%lx", round,
                  (unsigned long)result.entry_pa);
            CHECK(result.md_bridge_installed,
                  "round %d md bridge installed", round);
        }
        free(md);
        s5l8900_free(&machine);
    }
    free(tree);
    free(kernel);
}

int main(void) {
    test_real_kernel_accepted();
    test_digest_rejection();
    test_cross_build_rejection();
    test_bringup_configure();
    test_full_bringup();
    printf("ios4 kernel patch: %u passed, %u failed, %u skipped\n",
           g_passes, g_failures, g_skips);
    return g_failures ? 1 : 0;
}
