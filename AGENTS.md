硬件参考资料

https://docs.waveshare.net/ESP32-S3-ePaper-1.54

原理图 示例项目都在里面

硬件版本是V2

踩过的坑记在docs下

- docs/serial-setup.md — 串口(USB)配置方式，及「如何让串口保持连接」
- docs/gpio6-epd-power.md — GPIO6 (EPD3V3_EN) 实测不控制屏幕供电的踩坑记录

## 配置方式

两种配网：蓝牙(BLE) 和 USB 串口。串口配置及「如何让串口保持连接」见 docs/serial-setup.md。

关键点：设备 deep sleep 时 USB-Serial-JTAG 断电、COM 口消失，串口只在配置模式(按住 BOOT 唤醒 / 首次上电)下保持连接。