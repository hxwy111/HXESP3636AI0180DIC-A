# 360×360 圆屏显示与 CST816D TP 示例

本工程运行 LVGL widgets 组件演示，通过 CST816D 提供触摸输入。NV3003A 使用原有 QSPI 显示驱动。当前工程使用 ESP-IDF 5.5.4，dependencies.lock 锁定 LVGL 8.4.0。

## 硬件与配置

- CST816D：SDA=GPIO15、SCL=GPIO14、INT=GPIO11、RST=GPIO40，7 位地址 0x15，100kHz。
- components/touch/touch_config.h 集中配置引脚、量程、方向和恢复参数。
- 沿用 Arduino 上板修正的 X 轴反转（TOUCH_REVERSE_X=1）；Y 不反转，不交换 XY。
- 原始量程配置为 0～359，映射到屏幕坐标 0～359；超范围报告被拒绝并输出限频日志。
- 使用 ESP-IDF 原生 driver/i2c_master.h，不依赖 Arduino/Wire，不混用旧版 I²C 驱动。
- TOUCH_USE_EXISTING_I2C_BUS=0（默认）：由 TP 创建 I²C0，随后 RTC/IMU 可获取该句柄并添加各自设备。
- 仅当其他模块已在相同 GPIO15/14 上创建原生 I²C0 时，才设 TOUCH_USE_EXISTING_I2C_BUS=1；其他任务不得访问 TP 设备句柄或删除共享总线。
- LCD 保持 20MHz QSPI、RGB565 字节顺序及 50% 背光实际上限；绘制缓冲改为 40 行双 DMA 缓冲。

## 任务与时基

main 初始化背光、LCD、TP，再启动唯一 LVGL 任务。中断仅置标志，I²C 读取、触点解析、事件队列及 LVGL 对象操作均在 LVGL 任务中执行。输入回调将按下、移动、松手交给原生 widgets 演示，界面不再创建自定义诊断覆盖层或定时器。

驱动跟踪主触点 ID，主触点释放后等待全部手指松开；连续通信失败时释放指针并自动复位重试。RST 拉低 11ms 后释放，等待至少 120ms。运行中的恢复采用状态机，避免长延时阻塞界面。TP 未应答时显示仍可启动并重试，资源初始化失败时输出错误。未启用固件升级。

LV_TICK_CUSTOM=0 时，LVGL 任务通过 esp_timer_get_time 累计真实毫秒并调用 lv_tick_inc；开启自定义时基时不重复增加 tick。

## 启动日志说明

旧代码在创建 I²C0 前调用 i2c_master_get_bus_handle 检查总线是否存在。ESP-IDF 5.5.4 在端口未初始化时会直接输出错误，并返回 ESP_ERR_INVALID_STATE。旧代码随后创建总线，因而同一次启动中仍能识别 CST816D。

现默认明确创建 I²C0，不再提前查询不存在的句柄，也未屏蔽 I²C 驱动日志。用户提供日志中的 addr=0x15、chip=0xB6、fw=0x01、awake=1 表明当次初始化已完成 TP 寄存器通信。PC 端 ClearCommError/等待设备重连是串口连接异常，仅凭该日志不能判定具体原因，需与 I²C 初始化提示分别排查。

## 构建与验收

在已加载 ESP-IDF 5.5.4 的终端进入本工程目录，执行 idf.py build。烧录使用 idf.py -p <实际串口> flash monitor。

启动后确认 CST816D 识别日志和 LVGL_PORT first frame ready。界面仅显示 LVGL widgets 演示，验证按钮、滑条、列表滚动、左右方向、快速点击、长按及闲置后首次触摸。实际触摸效果需上板验证。

## 360 像素布局和刷新优化

原生 widgets demo 将 321～719 像素归为中屏，Profile 中两个按钮固定 120 像素并分配到不同网格列，360 像素下会挤压重叠。ui_demo 在创建演示后按标签定位这两个按钮，将它们放入跨整个面板的等宽 Flex 行，间隔 12 像素。启动时校验实际边界并输出按钮宽度和间距；保留原有按钮状态与其余 LVGL 组件。

显示刷新周期由 30ms 改为 16ms，输入周期 10ms，任务最短让出 1ms，并扣除绘制耗时避免额外休眠。40 行 RGB565 双缓冲共占 57,600 字节内部 DMA RAM，满屏分块由 18 次减少到 9 次。像素传输改为异步队列 DMA，使一块缓冲发送时可在另一块绘制；wait_cb 和每轮任务结束时回收事务，确认 DMA 完成后才通知 LVGL 释放缓冲。窗口命令仍同步发送，CS 和 32h + 002C00h 时序保持一致。

16ms 是刷新调度目标，不是已实测帧率。20MHz 四线总线传输 360×360 RGB565 的纯像素理论耗时约 25.92ms，满屏连续刷新理论上限约 38.6 帧/秒，实际还包含绘制、命令和调度开销；局部刷新可更快。需上板确认滚动、切页、Analytics 动画、连续触摸和 Invite/Log out 间距。未增加 TP TEST 或帧率覆盖层。

Shop 页的 Top products 与 Notification 在窄屏下改为各占整行、上下排列，保留原有滚动和复选框。商品行采用图片、可换行的名称/分类、价格三列；价格列按内容保留宽度，列间距 8 像素，避免 Blue jeans 和金额重叠。启动日志输出商品行数量，需上板检查第三页全部五条商品及通知勾选。
