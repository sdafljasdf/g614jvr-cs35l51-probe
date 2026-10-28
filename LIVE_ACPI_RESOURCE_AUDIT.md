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

## 2026-10-28 固定 _CRS 与 SPI3 所有权复核

CRS_STATIC_EQ_RUNTIME = YES（针对本次捕获的 DSDT 方法体在正常 AML 求值下的语义推断；不是执行 _CRS 的实测）。方法直接构造并返回固定 ResourceTemplate，无 If/Switch、区域读取或其他方法调用；仍不把此结论等同于 GPIO 电平或排除运行时命名空间变化的直接证据。

SPI3_NODE = IOService:/AppleACPIPlatformExpert/PC00@0/AppleACPIPCI/SPI3@13，Intel 8086:7a79，PCI 00:13.0（IORegistry pcidebug 0:19:0 为十进制）。
SPI3_PROVIDER = AppleACPIPCI / IOPCIDevice。
SPI3_ATTACHED_DRIVER = 未观察到 SPI 驱动子节点。
SPI3_ACTIVE_CLIENTS = IOService 树未显示客户端；该观察不能证明所有打开者不存在。
SPI3_CONTROLLER_OWNED = UNKNOWN；IORegistry 当前输出没有可用于确认 isOpen 的公开状态，不能仅由没有子节点判定 NO。

GPI0 为 INTC1085，相关 VoodooGPIO 模块已加载；这本身不证明 GPI0 被该模块独占，也不能证明空闲。上游 IntelLPSSSPIController::bind 及 IntelSPIChipSelects 中有 isOpen/open 拒绝冲突的逻辑，但它们尚未与独立 G614JVRProbe 生命周期及严格写白名单连接；前者的电源、复位和恢复路径也尚未满足本次安全约束。

本轮授权对已确认 CS GPIO 在探测前设置 HIGH，以及每个 CS1 transaction 的 LOW/HIGH；这撤销此前不得在事务前设置 inactive 的限制。GPIO:0140、00C0 和未知 GPIO 写仍禁止。当前未运行探针、未获得读值。
