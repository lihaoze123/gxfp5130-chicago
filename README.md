# GXFP5130 ChicagoHS for NixOS

华为 MateBook X Pro 2024 的 GXFP5130 指纹传感器原生 Linux 实现：内核字符设备驱动、TLS／配置与校准、图像预处理、特征提取、几何匹配、录入策略，以及匹配成功后的模板学习和持久化。运行时不需要 Windows DLL、模拟器或旧开发仓库。

目前验证的机器为 VGHH-XX，ACPI `GXFP5130:00`，固件 `GF_GCC_EC_20057`，图像尺寸 80×64。其他机器／固件尚未验证。这里保留了可构建的 libfprint 核心源码及字符设备发现扩展；软件包只启用 `gxfp` 驱动。部分原有 libfprint 框架兼容代码仍在源码中，但 Chicago 识别路径使用原生 Chicago 匹配器。

## 获取源码

```bash
git clone https://github.com/lihaoze123/gxfp5130-chicago.git
cd gxfp5130-chicago
```

## 准备本机私有文件

需要来自**同一台机器**的三个文件，不能使用他人的密钥或校准。文件在系统运行时读取，不能写进 flake、Git 或 Nix store。

| 文件 | 来源／用途 |
| --- | --- |
| `psk_raw32.bin` | 32 字节 TLS PSK，从自己的 Windows `Goodix_Cache.bin` 解封 |
| `chicagohs_cfg.bin` | 224 字节传感器配置，从自己的 `gfspi.dll` 提取 |
| `goodix_calib.dat` | Windows `C:\ProgramData\Goodix\goodix_calib.dat`，含传感器身份和校准 |

如果已经使用本项目的前身，`/var/lib/fprintd/gxfp/` 中的文件可以直接复用，无需重新导入。已有 `/var/lib/fprint/<用户>/gxfp/chicago-v1/` 模板也可复用。

首次准备 PSK 时，在原厂 Windows 上用 Python 运行：

```powershell
python tools/windows-unseal.py --cache "C:\路径\Goodix_Cache.bin" --out-psk psk_raw32.bin
```

该工具使用 Windows DPAPI，只解封已有缓存，不重新配钥。若 DPAPI 解封失败，不要用随机密钥替代。将 PSK 和校准带回 Linux；配置可由导入工具直接从 Windows 分区的 `gfspi.dll` 提取：

```bash
nix build .#tools -o result-tools
sudo ./result-tools/bin/gxfp-chicago-import \
  --psk /私有目录/psk_raw32.bin \
  --calibration /Windows挂载点/ProgramData/Goodix/goodix_calib.dat \
  --config /Windows挂载点
```

`--config` 也接受 DLL 路径或已提取的配置文件。导入前先停止正在运行的 fprintd。工具校验长度、校准版本及已测试配置的 SHA-256，拒绝覆盖内容不同的现有文件，写入 `/var/lib/fprintd/gxfp/`（目录 0700，文件 0600）。未知配置版本需要先分析，当前工具不会直接安装。

## NixOS 安装

将 GitHub 仓库作为 flake 输入：

```nix
{
  inputs.gxfp5130Chicago.url = "github:lihaoze123/gxfp5130-chicago";
  # 可选：跟随系统 nixpkgs；换版本后应重新构建验证。
  # inputs.gxfp5130Chicago.inputs.nixpkgs.follows = "nixpkgs";

  outputs = { nixpkgs, gxfp5130Chicago, ... }: {
    nixosConfigurations.laptop = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./configuration.nix
        gxfp5130Chicago.nixosModules.default
        { hardware.gxfp5130Chicago.enable = true; }
      ];
    };
  };
}
```

完成私有文件导入后执行 `sudo nixos-rebuild switch --flake /你的系统配置#laptop`，重启以加载对应系统内核构建的 `gxfp` 模块。检查 `ls -l /dev/gxfp`、`fprintd-list "$USER"`。内核模块按 `boot.kernelPackages.kernel` 构建，不要求修改固件。

模块安装设备驱动、fprintd 和三个用户工具，**默认关闭各 PAM 服务的指纹认证**。登录、sudo、锁屏需要自行配置，例如：

```nix
security.pam.services.sudo.fprintAuth = true;
```

模块默认开启引导和分数所需的日志。若只用标准 fprintd 命令，可设置 `hardware.gxfp5130Chicago.debug = false;`；此时工具无法保证取得分数／就绪提示。

若以前用过临时 fprintd 测试服务，先移除 `/run/systemd/system/fprintd.service` 及临时 drop-in，并恢复被临时替换的 `/etc/dbus-1`，再重建／重启；临时服务会遮蔽安装的服务。不要删除 `/var/lib/fprint` 或 `/var/lib/fprintd/gxfp`。

## 录入与验证

录入引导（默认右手食指）：

```bash
sudo gxfp-chicago-enroll --user "$USER"
```

需完成 12 个有效阶段。初始化时移开手指，看到就绪提示再触摸，获得结果后完全抬起。按方向提示小幅改变落点、覆盖中间和两侧，每次保留部分重叠，避免始终只录固定位置。重新录入会替换该手指的现有模板。

持续验证复用现有模板，不区分同指／异指标签，每轮输出是否匹配及得分：

```bash
sudo gxfp-chicago-verify --user "$USER"
```

每轮完全抬手后按 Enter；输入 `q` 结束。两种工具都接受 `--finger left-index-finger` 等标准 fprintd 手指名称。得分是匹配器内部评分，**不是百分比或概率**；重试／错误可能没有得分。当前参数采用 selector 207 和原生判定，未为提高通过率降低阈值。

也可直接使用 `fprintd-enroll -f right-index-finger "$USER"` 和 `fprintd-verify -f right-index-finger "$USER"`。标准 D-Bus 接口只返回状态，不提供数值分数；引导工具从该轮服务日志中读取分数，并等待末尾日志送达。

工具只保存必要统计和日志，不保存图像、描述子或模板，存放于 `~/.local/state/gxfp5130-chicago/`。成功验证时，符合学习条件的样本可以更新模板；fprintd 会保存发生变化的模板。拒绝、取消或失败不触发持久化成功学习。

## 当前验证结果与限制

一轮真实 fprintd 测试：同指匹配 9/10，异指匹配 0/10，错误／超时 0；日志确认有 3 次成功匹配触发模板学习并保存。样本量很小，不能据此估算安全认证的误接受率。用户后续试用反馈基本可用，但同指仍受落点、角度和录入覆盖范围影响。

保留的无设备测试覆盖原生预处理、OTP／FDT 策略、特征与模板序列化、几何匹配、录入及学习链路。部分来自原匹配器的 DLL 向量测试没有私有向量时会跳过；本仓库不分发这些向量，不声称已经完整复刻原厂所有行为。

```bash
nix build .#fprintd .#kernel .#tools
nix build .#matcher-tests   # CTest，测试结果在输出的 LastTest.log
```

## 开发与维护上下文

详见 [实现架构](docs/ARCHITECTURE.md)、[验证证据与边界](docs/VALIDATION.md) 和
[迁移及排错](docs/MAINTENANCE.md)。

## 源码结构与许可证

- `kernel/`：GXFP5130 eSPI 内核驱动。
- `libfprint/libfprint/drivers/gxfp.c`：真正的 libfprint 设备、录入／验证／学习接入。
- `libfprint/libfprint/drivers/gxfpmoc/`：传输、会话、原生预处理和 Chicago 匹配器。
- `nix/`：软件包、NixOS 模块及 fprintd 学习持久化补丁。
- `tools/`：私有文件导入、录入引导、持续验证及导入辅助代码。

来源和许可证见 [NOTICE.md](NOTICE.md) 与 `LICENSES/`。依赖源码已收进本仓库，不需要另行克隆此前的实验项目。私有配置、密钥、校准、指纹图像和用户模板均不包含在仓库中。
