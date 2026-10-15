/*
 * vgnbatt.c — 查询 VGN Mouse 2.4G Receiver (Compx 3554:f503) 的鼠标电量
 *
 * 协议（VGN 官方网页驱动 hub.vgnlab.com JS 逆向 + Fan4Metal/mouse_tray 交叉验证）:
 *   OUT report 8 (usagePage 0xFF02/usage 2 collection), 17 字节:
 *     08 04 00×14-1 49        49 = (85 - 4 - 8) 校验
 *   IN report 8, 17 字节:
 *     08 04 00 .. .. 02 % ch mV_hi mV_lo .. 51   (Σ mod 256 = 0x55)
 *
 * ★ macOS 关键差异: IOHIDDeviceSetReport 的 data 参数必须带 reportID 前缀字节
 *   (17B, Chrome WebHID 同款形态)。剥前缀的 16B hidapi/Windows 形态在这颗
 *   固件上线上报文缺 reportID, 设备不路由、无响应。
 *
 * 编译:
 *   clang vgnbatt.c -o vgnbatt -framework IOKit -framework CoreFoundation
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/hid/IOHIDLib.h>
#include <CoreFoundation/CoreFoundation.h>

#define VENDOR  0x3554
#define PRODUCT 0xf503

static int g_got = 0;

static void report_cb(void *ctx, IOReturn res, void *sender,
                      IOHIDReportType type, uint32_t reportID,
                      uint8_t *report, CFIndex len) {
    (void)ctx; (void)sender; (void)type;
    if (res != kIOReturnSuccess) return;

    /* report[] 即线上 17 字节报文: [0]=08 [1]=cmd [2]=status
     * [6]=电量% [7]=充电 [8..9]=电压 mV [16]=校验 */
    if (reportID == 0x08 && len >= 17 && report[1] == 0x04 && report[2] == 0x00) {
        unsigned mv = ((unsigned)report[8] << 8) | report[9];
        printf("鼠标电量: %u%%  (%s, %u mV)\n",
               report[6], report[7] ? "充电中" : "2.4G 无线", mv);
        g_got = 1;
    }
}

int main(void) {
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
        fprintf(stderr, "[X] 未找到 VGN 接收器 (%04x:%04x)\n", VENDOR, PRODUCT);
        return 3;
    }

    const void *devs[8] = {0};
    CFIndex cnt = CFSetGetCount(set);
    if (cnt > 8) cnt = 8;
    CFSetGetValues(set, devs);

    int ok = 0;
    for (CFIndex i = 0; i < cnt && !ok; i++) {
        IOHIDDeviceRef d = (IOHIDDeviceRef)devs[i];
        uint32_t up = 0, maxout = 0;
        CFTypeRef p;
        if ((p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDPrimaryUsagePageKey))))
            CFNumberGetValue(p, kCFNumberSInt32Type, &up);
        if ((p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDMaxOutputReportSizeKey))))
            CFNumberGetValue(p, kCFNumberSInt32Type, &maxout);
        if (up != 0xFF05 || maxout != 17) continue;   /* 只要 vendor 接口 */

        uint8_t *buf = calloc(1, 512);
        IOHIDDeviceRegisterInputReportCallback(d, buf, 512, report_cb, NULL);
        IOHIDDeviceScheduleWithRunLoop(d, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        if (IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone) != kIOReturnSuccess) continue;

        uint8_t q[17] = {8,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        q[16] = (uint8_t)(85 - 4 - 8);                /* 0x49 */
        for (int t = 0; t < 3 && !g_got; t++) {
            IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, 0x08, q, 17);
            for (int w = 0; w < 15 && !g_got; w++)
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
        }
        ok = g_got;
    }

    if (!ok) {
        fprintf(stderr, "[X] 设备未响应——鼠标可能在深度睡眠，移动几下鼠标后重试\n");
        return 4;
    }
    return 0;
}
