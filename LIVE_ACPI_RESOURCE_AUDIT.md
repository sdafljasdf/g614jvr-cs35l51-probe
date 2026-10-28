# 当前系统 ACPI 资源审计 — 2026-10-28

MaciASL 1.6.5 本轮从运行中的 macOS 导出 ACPI 表组；`System DSDT` 时间 16:44，DSDT 原始 604704 字节、checksum 0、SHA256 `7c3a6fbc3bfd3e18972b6a954fb787c623d2f98b587f731a47106db7e01b5355`，并由 iASL 20200925 反编译。39 张表已保存到工作区 LIVE_ACPI_EVIDENCE；导出的表组含本机名称，不纳入公开 Git 仓库。系统启动时间 16:02:55。当前 IORegistry 有 SPK1 / CSC3551，_STA=15，SPI3 对应 Intel 8086:7a79。

下列顺序来自本次读取的、当前加载 DSDT 的 `\_SB.PC00.SPI3.SPK1._CRS` 方法体；它无条件返回固定资源模板。它是当前加载 AML 的静态反编译，并非对运行时 IOACPIPlatformDevice::evaluateObject(_CRS) 的直接调用结果；当前 IORegistry 未暴露 `_CRS` 返回缓冲区。抓取的其他 SSDT 未发现 SPK1 字节字符串，但这不足以证明不存在所有动态修改。实时 BIOS 版本在伪装 SMBIOS 下无法独立读取，BIOS 322 仅为此前 Windows 证据。

LIVE_SPI_RESOURCE_0 = SpiSerialBusV2 deviceSelection 0；PolarityLow，FourWire，8 bit，4 MHz，ClockPolarityLow，ClockPhaseFirst，controller \_SB.PC00.SPI3，Exclusive。
LIVE_SPI_RESOURCE_1 = 同上，deviceSelection 1。

按 GPIO 描述符顺序：

| 资源 | 类型 | pin | restriction / direction | pull | sharing / interrupt |
|---|---|---|---|---|---|
| LIVE_GPIO_RESOURCE_0 | GpioIo | GPI0:0x01D4 | OutputOnly | PullUp | Exclusive；非 interrupt |
| LIVE_GPIO_RESOURCE_1 | GpioIo | GPI0:0x0140 | OutputOnly | PullDown | Exclusive；非 interrupt |
| LIVE_GPIO_RESOURCE_2 | GpioIo | GPI0:0x00C0 | InputOnly | PullUp | Exclusive；非 interrupt |
| LIVE_GPIO_RESOURCE_3 | GpioIo | GPI0:0x0141 | InputOnly | PullUp | Shared；非 interrupt |
| LIVE_GPIO_RESOURCE_4 | GpioInt | GPI0:0x0141 | 中断输入 | PullUp | Shared，Edge，ActiveBoth，debounce 0x0064 |

该表与先前声称的 OutputOnly=0、InputOnly=4、None=1 不一致；不能确定此前描述的数据或解析来源，不能将其归因于任何特定错误。固定 Linux v6.12 对 10431B93 的 quirk 将 GPIO 资源 0 定义为第二片选、资源 1 定义 reset、资源 2 定义 speaker ID，并将第一颗作为 native CS。故这是针对本机平台的强静态证据：CS0 native、CS1 GPI0:01D4、reset GPI0:0140、speaker ID GPI0:00C0。它不是芯片在线、GPIO 实际电平、所有权或时序的实测。

USER_STATIC_TEMPLATE_MATCH = YES
RUNTIME_CRS_EVALUATED = NO
CS1_IDLE_HIGH_PROVEN = NO
PROBE_ONLY_LIFECYCLE = NOT_IMPLEMENTED
READY_FOR_REAL_SPI = NO

本次未调用 SPI、GPIO 或功放接口，也未对设备写入。
