# 实现上下文

## 设备和最初的问题

已实测设备是 MateBook X Pro 2024 VGHH-XX，BIOS 1.20，ACPI 只有
`GXFP5130:00`，固件 `GF_GCC_EC_20057`。最初在 Linux 下 TLS、配置上传、
手指事件和 80×64 图像都能取得，但图像主要是列条纹和噪声；仅用 FPN 去除
背景后，特征不稳定。无手指的 manual measurement 也常返回 `touchflag=0x003f`，
抬手等待会很长。这说明“有事件、有非零图像”不足以证明图像和暗帧有效。
用户同时报告原厂 Windows 录入也不容易，PC Manager 表示无更新。

没有证据证明 20057 必须刷成 20068，也没有在此过程中更新或重新配钥。
当前解法是校准、原生预处理、原生匹配及生命周期修正。请勿仅凭固件字符串
推断 OTP 布局，或把跨机器校准／配置兼容性当作已知事实。

## 分层与数据流

1. `kernel/` 提供 ACPI/eSPI、IRQ、命令与 FIFO 字符设备 `/dev/gxfp`。
2. `gxfpmoc/src/io`、`proto`、`cmd`、`tls` 封装读取、Goodix 帧和 mbedTLS PSK 会话。
3. `src/flow/session.c` 管理打开、配置、初始化、FDT、采集、清理和事件调度。
4. `src/algo/image/chicago_native.c` 与 `chicago_backend.c` 处理解码后的原始
   80×64、12 位图像，输出原生增强结果。
5. `libfprint/libfprint/drivers/gxfp.c` 是 `FpDevice` 驱动，构造 Chicago probe，
   调用录入／匹配／学习，并报告标准 libfprint 状态。
6. fprintd 提供标准 D-Bus 服务；补丁把成功识别中改变的模板保存到磁盘。

`libfprint` 的设备发现包含 `FPI_DEVICE_UDEV_SUBTYPE_CHARDEV` 与 ACPI ID 支持。
所以不能只把 `gxfp.c` 放进任意未修改的发行版 libfprint 就期望能找到设备。
当前软件包只编译 `gxfp` 驱动；其余框架源码是支持可构建核心的基础。

### OTP、DAC 和 FDT

`session_apply_chicagohs_cfg()` 读取 OTP，保存前 16 字节为传感器身份；
由 `src/algo/sensor_cfg.c` 解析冗余 DAC 与时间参数，应用配置并上传。
`GXFP_CHICAGO_DAC_POLICY=windows` 选择已恢复的 Windows DAC 解析策略，
`GXFP_CHICAGO_OTP_TIMING=1` 应用 OTP 时间参数。

两组 DAC 数据涉及 OTP `0x32–0x35` 和 `0x2e–0x31`；当前解析器还处理冗余恢复，
不只是按“非零选第一组”。`GXFP_CHICAGO_DAC_BASE` 属于诊断覆盖选项，生产模块
不设置它。FDT 运行参数与手指上下沿的状态转换在 `session.c`、`flow/fdt.c`
中维护。FDT 的 touchflag 是检测通道状态，不能当成图像质量或真实纹路判据。

初始化必须在无手指时完成：暗帧会参与后续去背景。就绪提示来自服务的
`Finger needed 1`，而非客户端较早打印的 `Verify started!`。

### 两条预处理用途不能混淆

会话在原始图像阶段调用 image observer；驱动从同帧 raw 数据构造原生 Chicago
raw probe。随后会话输出自己恢复的 `chicago_native.c` 增强图像。
`gxfp_chicago_runtime_prepare_enhanced_probe()` 用该增强图像重新提取用于匹配的
特征、质量、覆盖和度量，同时保留 raw probe 中的分辨率、live auxiliary 和
坏采集判定信息。两者必须来自同一帧，不能拿另一帧或另一条预处理路径的
元数据来拼接。

`src/algo/match/goodix-chicago-preprocess.c` 也被带入，其类型及 raw probe
路径需要它。它不替代会话输出的 native enhanced image。这一组合有合成链路
和适配器差分检查支撑；不能据此声称与原厂完整调用链逐字节等价。

native backend 的校准文件是 16 字节头加 `0x224b0` payload。第一矩阵位于
payload 偏移 8，版本位于 `0x22490`，应为 `Preprocess_v_1.01.01`（含终止符）。
backend 用矩阵初始化系数和自适应参考；匹配侧的校准加载还验证身份绑定。
内部为了经过既有 12 位图像接口返回 8 位结果，采用
`ceil(v*4095/255)` 编码，驱动再用整数 `pixel*255/4095` 恢复。

## 识别、模板与学习

### 不做像素逐点匹配

80×64 只能覆盖手指的一小块区域。比较的是提取出的局部特征、描述信息和几何
一致性；几何匹配寻找相对位移／旋转和有效重叠，并经过后续拒绝规则。手指
位移过大、旋转、按压形变或重叠不足仍会失败。图像相似或像素误差小，都不能
替代最终匹配验证。

单次验证采一帧 probe，对录入模板中的多条 subtemplate 进行比较，选择有效
结果。当前使用 selector 207，驱动接受最终 `score > 0`；此最终分数已经包含
内部决策语义，不应把它当成一个可随意降低的通用相似度阈值。负值也不要未经
源代码和测试确认就解释成固定错误类型。旧 SIGFM 阈值不参与当前识别。

### 多次录入和位置提示

`goodix-chicago-enrollment` 与 engine policy 共同管理 12 个有效阶段，模板最多
可携带 50 条记录。方向提示、deferred 样本及恢复逻辑可能导致记录数大于
有效阶段数；实际成功录入曾有 20 records / 12 accepted。

方向提示日志 `position-retry=1/2/3/4` 分别表示下／上／右／左。曾把方向提示映射
到 `FP_DEVICE_RETRY_CENTER_FINGER`，导致 fprintd 只显示 `finger-not-centered`，
诱导用户每次回到同一落点。当前映射为 `FP_DEVICE_RETRY_GENERAL`，引导工具根据
日志显示方向；没有放宽录入策略。其改善真实覆盖范围的效果尚未专门验证。

### 模板身份和持久化

模板使用 `FPI_PRINT_RAW` 自定义数据和主机存储，设备 namespace 为 `chicago-v1`。
数据包含 gallery、实际 OTP 身份和校准关联信息。旧 SIGFM 模板不可直接用于
Chicago；换传感器或校准身份不符应重新核实／录入，而不是强行改绑定。

匹配成功后，对符合条件的样本执行 `goodix_chicago_runtime_study_print_data()`。
如有更新，修改匹配到的 `FpPrint` 的 `fpi-data`。学习失败会记录警告，但不
推翻已确认的匹配结果。每次成功都不一定学习，也不一定改变模板。

`nix/chicago-study-persistence.patch` 在 verify／identify 前保存序列化快照，
成功完成后比较匹配模板的序列化字节，只有变化时调用存储保存。失败／取消
不走成功保存逻辑；这一补丁不是降低门槛或改变 D-Bus 匹配状态的机制。

## 生命周期约束

fprintd 录入前会 identify 来检查重复。空主机 gallery 不应消耗一次触摸：当前
立即报告无匹配。非空 identify 在报告结果后等待抬手再完成，避免 fprintd
立刻启动 enrollment、在手指仍在时重新采暗帧。

verify 在报告结果后完成，以适配 fprintd 的 VerifyStop／取消流程。录入阶段
等待抬手后才允许下一次触摸。驱动操作期限目前是录入 600 秒、其他 90 秒。
修改这些状态和清理时序时，必须同时考虑客户端结束和服务再激活的行为。
