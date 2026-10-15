/*
 * vgnbatt.c — 查询 VGN Mouse 2.4G Receiver (3554:f503) 电量
 *
 * 协议来源: Fan4Metal/ATK_tray models.py (社区逆向)
 *   protocol 1: 17B output report, ID=0x08, [1]=0x04, [16]=0x49 -> 响应[6]=电量 [7]=有线
 *   protocol 2: 64B output report, ID=0x08, [1]=0x7D [2]=0x72 [3]=0x02 [5]=0x01 [6]=0x07 [7]=0x01
 *              -> 响应[1]=0x72 [5]=0x07, [7]=电量
 *
 * 仅用系统 IOKit HID API。clang -framework IOKit -framework CoreFoundation
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/hid/IOHIDLib.h>
#include <CoreFoundation/CoreFoundation.h>

#define VENDOR  0x3554
#define PRODUCT 0xf503

static int g_got = 0;

static void print_hex(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (i % 8 == 0) printf("\n      [%02zu] ", i);
        printf("%02x ", p[i]);
    }
    printf("\n");
}

static void report_cb(void *ctx, IOReturn res, void *sender,
                      IOHIDReportType type, uint32_t reportID,
                      uint8_t *report, CFIndex len) {
    (void)ctx; (void)sender; (void)type;
    if (res != kIOReturnSuccess) { printf("\n  [!] input report err=0x%x\n", res); return; }

    uint8_t comb[300];
    size_t n = 0;
    comb[n++] = (uint8_t)reportID;
    for (CFIndex i = 0; i < len && n < sizeof(comb); i++) comb[n++] = report[i];

    printf("\n  [收到 INPUT REPORT] %zu 字节:", n);
    print_hex(comb, n);
    /* 线上报文: [0]=08(reportID) [1]=cmd回显 [2]=0成功 [6]=电量 [7]=充电 [8..9]=mV
     * comb[0] 是 IOHIDLib 的 reportID 参数，线上报文从 comb[1] 起 */
    if (n >= 11 && comb[1] == 0x08 && comb[3] == 0x00) {
        const char *what = comb[2] == 4 ? "BatteryLevel" : "?";
        printf("     >>> %s: 电量=%u%%  充电=%s  电压=%umV\n",
               what, comb[7], comb[8] ? "是" : "否",
               (comb[9] << 8) | comb[10]);
    }
    g_got = 1;
}

static const char *rtype(IOHIDReportType t) {
    switch (t) {
    case kIOHIDReportTypeInput:  return "IN  ";
    case kIOHIDReportTypeOutput: return "OUT ";
    case kIOHIDReportTypeFeature:return "FEAT";
    default:                     return "?   ";
    }
}

static const char *etype(IOHIDElementType t) {
    switch (t) {
    case kIOHIDElementTypeInput_Misc:
    case kIOHIDElementTypeInput_Button:
    case kIOHIDElementTypeInput_Axis:
    case kIOHIDElementTypeInput_ScanCodes:
    case kIOHIDElementTypeInput_NULL:
        return "IN  ";
    case kIOHIDElementTypeOutput: return "OUT ";
    case kIOHIDElementTypeFeature: return "FEAT";
    default:                       return "COLL";
    }
}

static void dump_elements(IOHIDDeviceRef d) {
    CFArrayRef els = IOHIDDeviceCopyMatchingElements(d, NULL, 0);
    if (!els) { printf("  (无 elements)\n"); return; }
    CFIndex n = CFArrayGetCount(els);
    printf("  HID elements (%ld):\n", (long)n);
    int coll = 0;
    for (CFIndex i = 0; i < n; i++) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(els, i);
        uint32_t up = IOHIDElementGetUsagePage(e), us = IOHIDElementGetUsage(e);
        uint32_t rid = IOHIDElementGetReportID(e);
        IOHIDElementType et = IOHIDElementGetType(e);
        if (et == kIOHIDElementTypeCollection) {   /* 只显示 application 集合 */
            if (IOHIDElementGetParent(e) == NULL) {
                printf("    [COLL app] usagePage=0x%04x usage=0x%04x %s%s\n",
                       up, us, (up == 0xFF02 && us == 0x0002) ? "  <<< 电量协议要求!" : "",
                       (up == 0xFF05) ? "  (之前发错的地方)" : "");
                coll++;
            }
            continue;
        }
        /* 非 collection：只关心 vendor 页或非零 reportID */
        if (up >= 0xFF00 || rid != 0)
            printf("    [%3ld] %s reportID=0x%02x usagePage=0x%04x usage=0x%04x\n",
                   (long)i, etype(et), rid, up, us);
    }
    printf("    (application collections: %d)\n", coll);
    CFRelease(els);
}

static int wait_report(int tenth_sec) {
    for (int w = 0; w < tenth_sec && !g_got; w++)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
    int g = g_got; g_got = 0; return g;
}

__attribute__((unused))
static void try_getreport(IOHIDDeviceRef d, IOHIDReportType t, uint32_t id) {
    uint8_t buf[64];
    CFIndex len = sizeof(buf);
    IOReturn r = IOHIDDeviceGetReport(d, t, id, buf, &len);
    if (r == kIOReturnSuccess && len > 0) {
        printf("  <- GetReport(%s, id=0x%02x) len=%ld:", rtype(t), id, (long)len);
        print_hex(buf, (size_t)len);
        if (len >= 7) printf("     电量候选 byte[6]=%u\n", buf[6]);
    } else {
        printf("  <- GetReport(%s, id=0x%02x) -> 0x%x\n", rtype(t), id, r);
    }
}

int main(int argc, char **argv) {
    int listen_only = (argc > 1 && strcmp(argv[1], "listen") == 0);

    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
    if (!mgr) { fprintf(stderr, "IOHIDManagerCreate 失败\n"); return 1; }

    const void *keys[] = { CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey) };
    const void *vals[] = { CFNumberCreate(NULL, kCFNumberIntType, &(int){VENDOR}),
                           CFNumberCreate(NULL, kCFNumberIntType, &(int){PRODUCT}) };
    CFDictionaryRef matching = CFDictionaryCreate(NULL, keys, vals, 2,
                          &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(mgr, matching);

    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set || CFSetGetCount(set) == 0) {
        fprintf(stderr, "[X] 未找到 %04x:%04x 设备\n", VENDOR, PRODUCT);
        return 3;
    }
    printf("[+] 匹配到 %ld 个 HID 接口\n\n", (long)CFSetGetCount(set));

    const void *devs[8] = {0};
    CFSetGetValues(set, devs);
    CFIndex cnt = CFSetGetCount(set) > 8 ? 8 : CFSetGetCount(set);

    for (CFIndex i = 0; i < cnt; i++) {
        IOHIDDeviceRef d = (IOHIDDeviceRef)devs[i];
        uint32_t up = 0, us = 0, maxout = 0, maxin = 0, maxfeat = 0;
        CFTypeRef p;
        char name[128] = "(?)";
        if ((p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDProductKey))))
            CFStringGetCString(p, name, sizeof(name), kCFStringEncodingUTF8);
        if ((p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDPrimaryUsagePageKey))))
            CFNumberGetValue(p, kCFNumberSInt32Type, &up);
        if ((p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDPrimaryUsageKey))))
            CFNumberGetValue(p, kCFNumberSInt32Type, &us);
        if ((p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDMaxOutputReportSizeKey))))
            CFNumberGetValue(p, kCFNumberSInt32Type, &maxout);
        if ((p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDMaxInputReportSizeKey))))
            CFNumberGetValue(p, kCFNumberSInt32Type, &maxin);
        if ((p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDMaxFeatureReportSizeKey))))
            CFNumberGetValue(p, kCFNumberSInt32Type, &maxfeat);

        printf("== 接口 %ld: %s  usagePage=0x%04x usage=0x%04x maxOut=%u maxIn=%u maxFeat=%u ==\n",
               (long)i, name, up, us, maxout, maxin, maxfeat);

        uint8_t *buf = calloc(1, 512);
        IOHIDDeviceRegisterInputReportCallback(d, buf, 512, report_cb, NULL);
        IOHIDDeviceScheduleWithRunLoop(d, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

        IOReturn ro = IOHIDDeviceOpen(d, listen_only ? kIOHIDOptionsTypeSeizeDevice
                                                     : kIOHIDOptionsTypeNone);
        if (ro != kIOReturnSuccess) {
            printf("  -> IOHIDDeviceOpen 0x%x（标准鼠标/键盘接口被系统占用属正常）\n", ro);
            continue;
        }
        printf("  -> IOHIDDeviceOpen OK (%s)\n", listen_only ? "seize+监听" : "普通");

        if (listen_only) {
            if (up < 0xFF00) continue;
            printf("  [Chrome WebHID 形态 (17B 含 reportID 前缀) x 25，请持续移动鼠标]\n");
            for (int q = 1; q <= 25; q++) {
                uint8_t p[17] = {8,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
                p[16] = (uint8_t)(85 - 4 - 8);   /* 0x49 */
                printf("  [#%d] 查询电量...\n", q);
                IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, 0x08, p, 17);
                wait_report(12);
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, true);
            }
            printf("  [轮询结束]\n");
            IOHIDDeviceClose(d, kIOHIDOptionsTypeSeizeDevice);
            continue;
        }

        dump_elements(d);

        /* 电量查询 (VGN 官方协议 + macOS 关键差异):
         * IOHIDDeviceSetReport 的 data 参数必须带 reportID 前缀字节 (17B),
         * 否则线上报文缺 reportID, 固件不路由 (Windows hidapi 则是剥前缀的 16B) */
        printf("  [电量查询]\n");
        for (int q = 1; q <= 3; q++) {
            uint8_t p[17] = {8,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
            p[16] = (uint8_t)(85 - 4 - 8);
            IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, 0x08, p, 17);
            wait_report(12);
        }
    }

    printf("\n[完] 探测完成\n");
    return 0;
}
