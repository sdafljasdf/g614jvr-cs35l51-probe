# G614JVR Phase A 写操作审计 — 未批准实机运行

范围：本地上游 fork 的现有 Intel LPSS 引擎，以及隔离的 ProbeSequence.hpp。该表是源码中的可能写入清单，不是实机计数。用户允许经确认的 GPIO 片选在事务期间 assert/deassert；禁止功放复位、SPKR_ID、未知 GPIO、功放寄存器写。当前没有已连接的探针实现可以证明满足。

| WRITE_TARGET | 地址 / 寄存器 | PURPOSE / 当前风险 |
|---|---|---|
| SPI_CONTROLLER_MMIO | 0x200 clock | 时钟开关/恢复；controller 写，尚未用于本机 |
| SPI_CONTROLLER_MMIO | 0x204 reset | prepareHardware / restorePrerequisites 可改变控制器复位；优先不复位，不满足时停止；如将来允许，先证明仅影响 SPI3、无其他工作设备、无功放 GPIO 或寄存器写 |
| SPI_CONTROLLER_MMIO | 0x238 clock gate | 交换期间时钟门控及恢复 |
| SPI_CONTROLLER_MMIO | 0x00 control0、0x04 control1 | 禁用/配置/启用和恢复 SSP |
| SPI_CONTROLLER_MMIO | 0x08 status | 写状态位以清状态；只允许在明确验证语义后使用 |
| SPI_CONTROLLER_MMIO | 0x28 timeout、0x2c protocol | 初始化和恢复 SSP |
| SPI_CONTROLLER_MMIO | 0x10 FIFO | 写入只读事务的地址、标志、填充和 clocking 字节；FIFO 写本身不能证明总线上不是功放写帧，必须强制精确帧格式 |
| CHIP_SELECT_CONTROL | 0x224 | native CS 选择、assert / deassert，cleanup 置 inactive；须验证为 native controller CS |
| GPIO | IntelSPIChipSelects::bind / setActive | 上游 GPIO-backed CS 可改变 GPIO；只允许证明为片选的 GPIO 在事务中 assert/deassert，不能使用 reset、speaker-ID、interrupt 或未知 GPIO；当前尚未实现证明和限制 |
| AMP_REGISTER | 上游常规驱动多处 | 默认完整驱动存在功放寄存器写；独立模式必须在调度/生命周期级别排除，而非仅依赖开关或不调用预期入口 |

当前 ProbeSequence.hpp 仅通过注入的 mock 接口发起最多四次只读请求，编译测试通过；它没有设备打开、MMIO、GPIO、功放接口，也未连接 Xcode kext。已有 upstream read-only 模式在成功后可 dump 大量寄存器，错误诊断还可能重复采样，不符合用户限制。用户所需的部署级别计数如下：

AMP_REGISTER_WRITES = 未实测（本轮实际无功放访问，发生次数 0）
AMP_RESET_GPIO_WRITES = 0（本轮无访问）
SPKR_ID_GPIO_WRITES = 0（本轮无访问）
UNKNOWN_GPIO_WRITES = 0（本轮无访问）
NATIVE_CS_OPERATIONS = 0（本轮无访问）
GPIO_CS_OPERATIONS = 0（本轮无访问）
SPI_CONTROLLER_RESET_WRITES = 0（本轮无访问）
CONTROLLER_WRITES = 0（未部署）
SPI_TRANSACTIONS = 0

必须先实现专用 kext、编译条件排除所有正常路径，仅经证据确认的原生或外部 CS、优先不复位控制器、仅 DEVID/REVID 读取帧、失败即停且无异步重试/功率恢复重试；覆盖初始化、失败清理、suspend/resume/stop 全调用图后才能证明。不能将离线 Mock 的测试结果等同实际路径审计。

## G614JVR 片选证据和限制

本机历史 DSDT SPK1 _CRS 包含 SPI DeviceSelection 0 和 1；两个输出 GPIO 资源引脚 GPI0:0x01D4 和 GPI0:0x0140，以及输入 GPI0:0x00C0。固定 Linux v6.12 10431B93 机型表指定 cs_gpio_index=0、reset_gpio_index=1、spkid_gpio_index=2；其 generic_dsd_config 在不存在设备属性时，将第一颗指定 native CS，第二颗指定该 GPIO。由此可得静态候选 CS0 native、CS1 external GPIO GPI0:0x01D4；不是当前电平、实际绑定或通信成功的实测。设备属性存在、资源或身份不匹配均须拒绝该回退。

Linux 要求在访问第一颗前确保外部 CS 未被选中；用户当前限制外部 CS 只在事务期间 assert/deassert，因此不能无条件照搬初始化时预先改变 GPIO 电平。若不能只读证明其处于 inactive，应停止并报告，不得擅自切换。为模拟设计增加 ChipSelectPolicy.hpp 和测试，尚未接入 kext。
