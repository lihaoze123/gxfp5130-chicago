# 安装、迁移与排错上下文

## 文件与环境约定

运行时私有文件在 `/var/lib/fprintd/gxfp/`，主机模板在 `/var/lib/fprint/`。
Nix 导入的是源码，不是这些私有文件。应保留目录／文件权限，勿把私有路径
直接作为 Nix 的 source 路径插值：那会把文件复制进 store。

模块设置的环境为：

| 参数 | 值／用途 |
| --- | --- |
| `GXFP_CHICAGO_CFG` | `1`，开启 Chicago 配置 |
| `GXFP_CHICAGO_PREPROCESS` | `native`，驱动要求原生增强路径 |
| `GXFP_CHICAGO_CALIB` | `/var/lib/fprintd/gxfp/goodix_calib.dat` |
| `GXFP_CHICAGO_OTP_TIMING` | `1` |
| `GXFP_CHICAGO_DAC_POLICY` | `windows` |
| `G_MESSAGES_DEBUG` | debug=true 时为 `all`，提供就绪／得分／方向日志 |

源码另支持 `FP_GXFP_PSK` 和 `GXFP_CHICAGOHS_CFG` 覆盖 PSK／配置路径，默认分别
为 `psk_raw32.bin` 和 `chicagohs_cfg.bin` 所在的上述私有目录。
`FP_GXFP_LOG` 和 `GXFP_CHICAGO_DIAG` 用于深入设备诊断，默认不开。
请勿常规启用未知诊断开关，也不要上传未经检查的完整设备日志。

## 正常 NixOS 服务与旧临时服务

正常安装依赖 NixOS 的 `services.fprintd.package`、D-Bus/polkit 资源和 systemd
包注册；设备访问包含 `/dev/gxfp rw`，udev 节点为 root 0600。用户通过 fprintd
获得系统策略授权，不需要直接打开设备。

此前开发为了绕开未重建的系统配置，曾使用：

- `/run/systemd/system/fprintd.service` 和 `chicago-native.conf` 临时 drop-in；
- `/etc/dbus-1` 临时指向 `/run/gxfp-fprintd-native-test/dbus-1`；
- 原目录链接记录于 `/run/gxfp-fprintd-native-test/dbus-directory.original`。

这些是本机实验状态，不是发行版安装方案。新工具不再修改服务、D-Bus 或
系统授权。正式迁移时先检查实际链接和 unit，停止 fprintd，把上述本次实验
创建的覆盖清掉，按 original 记录恢复 `/etc/dbus-1`，再 rebuild／重启。
恢复前应核对 original 指向有效的系统路径，不能照抄别人的链接。
本次整理保留当前运行包的 GC root，并未擅自切换系统或删除私有模板。

本机旧产物的唯一回退包在
`~/.local/state/gxfp5130-chicago/rollback-20261006.tar.zst`（私有 0600），清理清单
与该包同目录。它包含旧工作树、实验与私有素材，不应提交或公开。
需要恢复时先在权限 0700 的独立目录解包：

```bash
mkdir -m 700 /私有恢复目录
tar --zstd -xf ~/.local/state/gxfp5130-chicago/rollback-20261006.tar.zst \
  -C /私有恢复目录
```

归档中的 `result-*` 是旧 Nix store 链接，不是软件包本体；恢复源码后可重新
构建。当前临时服务使用的包另由状态目录中的 `runtime-fprintd` GC root 保留。
回退包不含 `/var/lib/fprint` 的现有主机模板；这些文件一直保留原位。

## 常见问题按层检查

1. **找不到设备**：看 ACPI 是否为 GXFP5130、`journalctl -k` 中 gxfp 初始化，
   `/dev/gxfp` 是否存在；核实模块与当前内核版本一致。
2. **不能打开／TLS 失败**：检查 PSK 是否32字节、权限及是否本机缓存；停止其他
   直接采集进程。不要重新配钥以掩盖文件或身份错误。
3. **配置或校准拒绝**：核实来源、224字节配置 hash、140480字节校准及版本。
   当前 native matcher 还验证 OTP 身份；复制别人的文件不适用。
4. **初始化迟迟不就绪**：完全移开手指，查看本轮日志及是否暗帧/FDT 卡住；
   不要在 `Verify started!` 时立即触摸。别通过删模板来修复设备等待。
5. **方向／录入反复重试**：采用 `gxfp-chicago-enroll` 看方向；12个阶段和实际
   records 不一定相等。标准 CLI 的通用重试不等于手指必须回到中心。
6. **有匹配状态、没有得分**：D-Bus 本来没有数值分数。启用模块 debug，使用
   引导工具，核实日志读取权限。工具从启动前 cursor 重新读取并等尾部日志，
   避免客户端退出时丢失最后一条。无有效 probe 的重试仍可能没有得分。
7. **独立诊断成功、fprintd失败**：看实际 ExecStart 和加载的 libfprint，是否
   仍是 SIGFM、是否旧模板 namespace、identify/录入重激活时是否已抬手。
   不要仅凭图像清晰度断言接入或匹配没问题。
8. **成功后学习没有保存**：看是否有 `study updated matched print` 和
   `Saved print updated during successful verification`；不是每个成功样本
   都符合学习条件。确认服务确实使用带持久化补丁的 fprintd。

只需状态／得分时可分享工具筛选后的统计；校准、PSK、缓存、模板、原始和
处理图像均属私有资料。要做新设备适配，先记录型号、ACPI、固件、OTP解析
结果与初始化阶段，不要求用户反复触摸来替代静态源码分析。
