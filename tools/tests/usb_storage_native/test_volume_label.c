#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FF_USE_LABEL TEST_LABEL_ENABLED
#define FF_LFN_UNICODE TEST_LABEL_UNICODE
#define FF_VOLUMES 2
#define DRIVE_STR_LEN 3
#if TEST_LABEL_BDL
#define MSC_HOST_BDL_API_SUPPORTED
#endif
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE,
       ESP_ERR_INVALID_SIZE, ESP_ERR_NOT_SUPPORTED };
typedef unsigned char BYTE;
typedef char TCHAR;
enum { FR_OK, FR_DISK_ERR };
typedef struct msc_host_vfs {
    char drive[DRIVE_STR_LEN];
    const void *bdl;
} msc_host_vfs_t;
typedef msc_host_vfs_t *msc_host_vfs_handle_t;

static unsigned label_calls, mapping_calls;
static BYTE mapped_drive;
static int fat_result;
static const char *fat_label = "";
static const char *expected_drive;
static const void *expected_bdl;
BYTE ff_diskio_get_pdrv_bdl(const void *bdl)
{
    ++mapping_calls;
    assert(bdl == expected_bdl);
    return mapped_drive;
}
int f_getlabel(const char *drive, TCHAR *label, void *serial)
{
    ++label_calls;
    assert(!strcmp(drive, expected_drive) && !serial);
    assert(strlen(fat_label) < 129);
    strcpy_s(label, 129, fat_label);
    return fat_result;
}
#include "volume_label.h"

int main(void)
{
    msc_host_vfs_t first = { "0:", (const void *)1 };
    char output[64] = "stale";
    assert(ofe_usb_volume_label(&first, NULL, sizeof(output)) == ESP_ERR_INVALID_ARG);
    assert(ofe_usb_volume_label(&first, output, 0) == ESP_ERR_INVALID_ARG);
    assert(!strcmp(output, "stale"));
    assert(ofe_usb_volume_label(NULL, output, sizeof(output)) == ESP_ERR_INVALID_ARG);
    assert(!*output && !label_calls && !mapping_calls);
#if FF_USE_LABEL && FF_LFN_UNICODE == 2
    msc_host_vfs_t second = { "1:", (const void *)2 };
    expected_drive = "0:"; expected_bdl = first.bdl; mapped_drive = 0;
    fat_label = "FIRST";
    assert(ofe_usb_volume_label(&first, output, sizeof(output)) == ESP_OK);
    assert(!strcmp(output, "FIRST"));
    expected_drive = "1:"; expected_bdl = second.bdl; mapped_drive = 1;
    fat_label = "SECOND";
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_OK);
    assert(!strcmp(output, "SECOND") && label_calls == 2);
    fat_label = "  USB \xC3\x84  ";
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_OK);
    assert(!strcmp(output, "  USB \xC3\x84"));
    fat_label = "";
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_OK && !*output);
    fat_label = "   ";
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_OK && !*output);
    fat_label = "1234567";
    char bounded[10]; memset(bounded, 'X', sizeof(bounded));
    assert(ofe_usb_volume_label(&second, bounded + 1, 8) == ESP_OK);
    assert(bounded[0] == 'X' && bounded[9] == 'X' && !strcmp(bounded + 1, fat_label));
    memset(bounded, 'X', sizeof(bounded));
    assert(ofe_usb_volume_label(&second, bounded + 1, 7) == ESP_ERR_INVALID_SIZE);
    assert(bounded[0] == 'X' && bounded[1] == '\0' && bounded[2] == 'X' && bounded[8] == 'X');
    char long_label[129], large_output[129];
    memset(long_label, 'L', 128); long_label[128] = '\0'; fat_label = long_label;
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_ERR_INVALID_SIZE && !*output);
    assert(ofe_usb_volume_label(&second, large_output, sizeof(large_output)) == ESP_OK);
    assert(!strcmp(large_output, long_label));
    long_label[63] = '\0';
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_OK && strlen(output) == 63);
    long_label[63] = 'L'; long_label[64] = '\0';
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_ERR_INVALID_SIZE && !*output);
    fat_label = "";
    assert(ofe_usb_volume_label(&second, output, 1) == ESP_OK && !*output);
    fat_label = "X";
    assert(ofe_usb_volume_label(&second, output, 1) == ESP_ERR_INVALID_SIZE && !*output);
    fat_result = FR_DISK_ERR;
    strcpy_s(output, sizeof(output), "stale");
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_FAIL && !*output);
#ifdef MSC_HOST_BDL_API_SUPPORTED
    const unsigned calls = label_calls;
    mapped_drive = FF_VOLUMES;
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_ERR_INVALID_STATE);
    mapped_drive = 0xff;
    assert(ofe_usb_volume_label(&second, output, sizeof(output)) == ESP_ERR_INVALID_STATE);
    assert(label_calls == calls && !*output && mapping_calls > 0);
#else
    assert(!mapping_calls);
#endif
#else
    assert(ofe_usb_volume_label(&first, output, sizeof(output)) == ESP_ERR_NOT_SUPPORTED);
    assert(!*output && !label_calls && !mapping_calls);
#endif
    puts("PASS: extracted read-only C label accessor, VFS drive mapping, bounds and configuration guards");
    return 0;
}
