# CapsWriter C++ 客户端

> **本项目参考了 [CapsWriter-Offline](https://github.com/HaujetZhao/CapsWriter-Offline)
> （v2.6，作者 HaujetZhao），用 C++ 重写了其「按住说话、松开上屏」的听写链路。**
> 原版是 Python 服务端/客户端（C/S）架构；本项目的功能语义、配置字段、模型选择
> 与规则格式均以其为参照，识别与标点模型亦取自原版发布的模型包（见下文
> 「模型来源」）。原版的核心特性——**完全离线、低延迟、高准确率、高度自定义
> 的热词/规则替换**——在本项目中保持对齐。

CapsWriter-Offline（v2.6）听写功能的纯 C++ 实现：按住热键说话、松开识别、
自动上屏。与原 Python 版本相比，它**不启动 Python 服务端、不走 WebSocket**，
而是通过 [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) 的 C API 在
本进程内直接完成识别（Paraformer）和标点恢复（CT-Transformer）。

| 模块 | 原 Python 版本 | 本 C++ 版本 |
| --- | --- | --- |
| 热键监听 | `pynput`（键盘+鼠标钩子） | `WH_KEYBOARD_LL` + `WH_MOUSE_LL`，支持阻塞（suppress）与短按补发 |
| 录音 | `sounddevice` 48kHz → 抽样 16kHz | WASAPI 共享模式，降混单声道 + 线性重采样到 16 kHz |
| 识别 | Python 服务端 + `sherpa_onnx.OfflineRecognizer.from_paraformer` | sherpa-onnx C API 进程内直调（参数一致：4 线程 / 80 维 / greedy_search），支持 `hotwords_file` 上下文热词加权 |
| 标点恢复 | `Punct-CT-Transformer` | 同模型，经 C API 调用 |
| 上屏 | `keyboard.write` / 剪贴板 + Ctrl+V | `SendInput`（`KEYEVENTF_UNICODE` 打字 或 剪贴板粘贴，可恢复剪贴板） |
| 末尾标点消除 | `trash_punc` 系列 | 同语义（按语义字符数阈值 + 指定应用强制） |
| 单击模式 | `hold_mode: false` 敲击切换录音 | 同语义状态机（快速敲击开始/结束，按住过久取消） |
| 规则替换 | `hot-rule.txt` 正则替换（`RuleCorrector`） | 同格式规则文件 + `std::regex`，文件改动自动热重载 |
| 繁体转换 | `zhconv`（词语级） | `LCMapStringW` 逐字转换（`zh-hant`/`zh-tw`/`zh-hk` 统一处理，词语级差异不覆盖） |
| 自动回车 | `enter_apps` | 同语义（按应用匹配，延迟后补发 Enter） |
| UDP 广播 | `udp_broadcast` | 同语义（`SO_BROADCAST` 多目标发送） |
| UDP 控制 | `udp_control`（START/STOP） | 同协议（监听端口收命令启停录音） |
| Qt 托盘 GUI | — | 无边框暗色窗口 + 系统托盘，与原版 `tray_gui.py` 视觉对齐；日志与识别结果分别在两个标签页实时显示（见 `gui/`） |

**模型与规则全部随项目自包含**：Paraformer 与 Punct-CT-Transformer 模型
位于本项目 `models/` 下（`model_root: ../..`，相对 exe 所在目录 `build/Release/`
解析），`hot-rule.txt` 规则文件在项目根，无需任何外部数据目录。

已对齐的原版行为：
- `suppress: true` 时钩子吞掉热键事件；短按（< `threshold`，默认 0.3s）自动补发，
  不影响 CapsLock / 鼠标侧键的正常使用
- 单击模式（`hold_mode: false`）：轻敲开始录音，再轻敲结束识别；
  按住超过 `threshold * 0.8` 则取消本次录音（与原版 `ShortcutEventHandler` 一致）
- 非阻塞模式下的切换键（CapsLock/NumLock/ScrollLock）在松开时补发恢复状态
- `paste` / `paste_apps` / `restore_clip` 粘贴策略
- 前台应用感知的末尾标点消除
- 识别与上屏全部在工作线程执行，不阻塞低级钩子（避免系统因钩子超时摘除监听）

未移植（后续可选）：音素级热词 RAG（`hot.txt`，可用 `asr.hotwords_file`
上下文加权部分替代）、数字 ITN 格式化（「十五六个」→「15~16个」）、
LLM 角色润色、文件转录（拖拽音视频出字幕/文本/时间戳）、托盘菜单热词管理、
60 秒分段听写（长音频整段识别）。

## 构建（MSVC + CMake）

1. 下载 sherpa-onnx 的 Windows x64 shared 预编译包（见其
   [Releases 页面](https://github.com/k2-fsa/sherpa-onnx/releases)）。
   注意资产命名：自 v1.13.x 起为
   `sherpa-onnx-v<版本>-win-x64-shared-MD-Release.tar.bz2`（`MD` 对应
   `/MD` 动态运行库；另有 `-MT-` 静态运行库与 `-no-tts` 精简版），
   而非早期的 `windows-x64-shared.zip`。解压到任意目录，如
   `C:\sherpa-onnx`。包内应含有
   `include/sherpa-onnx/c-api/c-api.h`、`lib/sherpa-onnx-c-api.lib`；
   运行 DLL 位于 `lib/`（旧版本在 `bin/`，构建脚本对两处都会复制）。

2. 配置并构建：

   ```bat
   cmake -B build -G "Visual Studio 17 2022" -A x64 ^
         -DSHERPA_ONNX_ROOT=C:/sherpa-onnx ^
         -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
   cmake --build build --config Release
   ```

   加上 `-DCMAKE_PREFIX_PATH=...` 会多构建一个 `capswriter-gui.exe`
   （Qt 托盘前端，Windows 系统托盘常驻）。Qt 找不到时只构建控制台版
   `capswriter.exe`，构建脚本会给一条提示。

   构建后 `build/Release/` 中生成 `capswriter.exe`，运行时所需 DLL
   （`sherpa-onnx-c-api.dll`、`onnxruntime.dll` 等）自动复制到同目录。

3. 模型：已内置在本项目 `models/` 目录（相对 `model_root: ../..` 解析）：
   - `models/paraformer-offline-zh/` — Paraformer **int8 量化版**
     （`model.int8.onnx` + `tokens.txt` + `am.mvn`，sherpa-onnx 官方格式）
   - `models/Punct-CT-Transformer/` — 标点恢复 CT-Transformer（fp32）
   若需重新下载或换回 fp32 版，见文末「模型精简与恢复」。

4. 运行：

   控制台版（后台常驻钩子进程，日志写入 stderr，Ctrl+C 退出）：

   ```bat
   capswriter.exe              :: 读取 exe 同目录的 config.yaml
   capswriter.exe --config D:\my.yaml
   ```

   Qt 托盘版（无边框暗色窗口 + 系统托盘，关闭按钮=隐藏到托盘；启动
   速度比控制台版稍慢是因为要先加载模型，建议保持后台常驻）：

   ```bat
   capswriter-gui.exe
   capswriter-gui.exe --config D:\my.yaml --log-file capswriter-gui.log
   ```

   按住 CapsLock（或鼠标侧键）说话，松开后识别结果自动输入到当前光标处；
   托盘版的「识别结果」标签页会同步显示每次的识别文本。

   GUI 版支持 `--screenshot <path>` 调试参数：启动并加载完成后 1.5 秒自动
   截图保存并退出，便于无人工参与的界面验证。

## 配置说明（config.yaml）

- `shortcuts`：快捷键列表，字段与 `config_client.py` 一致：
  `key`（`caps_lock`/`x2`/`f12`/`ctrl_r`/字母数字符号等）、
  `type`（`keyboard`/`mouse`）、`suppress`、`hold_mode`、`enabled`。
- `threshold`：短按判定阈值（秒），短按会取消识别并补发按键；
  单击模式下按住超过 `threshold * 0.8` 也会取消录音。
- `paste` / `restore_clip` / `paste_apps`：粘贴输出策略。
- `enter_apps`：输出完成后自动回车的应用（`app` + `delay` 秒）。
- `trash_punc` / `trash_punc_thresh` / `trash_punc_apps`：末尾标点消除。
- `traditional_convert` / `traditional_locale`：繁体转换（逐字）。
- `hot_rule` / `rule_file`：`hot-rule.txt` 正则替换与热重载。
- 录音保存（`save_audio`）与日记归档已移除：识别音频仅在内存中处理，不会在本地落盘。
- `udp_broadcast` / `udp_broadcast_targets`：识别结果 UDP 广播。
- `udp_control` / `udp_control_addr` / `udp_control_port`：
  外部程序向该端口发送 `START` / `STOP` 控制录音。
- `asr.*`：Paraformer 模型路径、线程数、`provider`（`cpu`/`cuda`）、
  解码方法、`hotwords_file`/`hotwords_score`（sherpa-onnx 上下文热词加权）。
- `punct.enabled` / `punct.model`：标点恢复开关与模型路径。
- `model_root`：相对模型路径的基准目录。
- `log_level`：日志级别（`DEBUG`/`INFO`/`WARNING`/`ERROR`）。

## 版本说明

代码面向 sherpa-onnx 1.10.x 及之后的稳定 C API 编写。若所选预编译版的
`c-api.h` 有出入，编译错误会直接指向 `asr_engine.cpp`，按该头文件微调即可。

## 模型来源

本项目两个模型与参考项目 **CapsWriter-Offline** 使用的完全相同，模型包
即取自其发布的 [Models Release](https://github.com/HaujetZhao/CapsWriter-Offline/releases/tag/models)
（v1.0 模型包），本地 `models/` 目录结构与其保持一致：

| 用途 | 模型 | 本地目录 | 需要文件 | 来源 |
| --- | --- | --- | --- | --- |
| 语音识别（ASR） | Paraformer **int8 量化版** | `models/paraformer-offline-zh/` | `model.int8.onnx` + `tokens.txt` + `am.mvn`（+ `config.yaml`） | [sherpa-onnx-paraformer-zh-2023-09-14](https://huggingface.co/csukuangfj/sherpa-onnx-paraformer-zh-2023-09-14)（`git clone` 后仅保留上述文件） |
| 标点恢复（PUNC） | Punct-CT-Transformer（fp32） | `models/Punct-CT-Transformer/sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12/` | `model.onnx` + `tokens.json`（+ `config.yaml`） | [CapsWriter-Offline v1.0 模型包](https://github.com/HaujetZhao/CapsWriter-Offline/releases/tag/v1.0)（或[百度网盘 提取码 eu4c](https://pan.baidu.com/s/1zNHstoWZDJVynCBz2yS9vg)） |

下载后按表放回对应目录即可，`config.yaml` 的 `asr.*` / `punct.model` 已默认
指向这些路径（`model_root: ../..` 相对 exe 所在目录 `build/Release/` 解析）。

**为什么是这两个模型**（参考原版 README 的引擎对比）：原版支持 Paraformer、
SenseVoice-Small、Fun-ASR-Nano、Qwen3-ASR 等多引擎，本项目选用与原版默认一致
的 Paraformer + CT-Transformer 组合——Paraformer 是其中**延迟最低**的引擎
（原版实测 20s 音频 CPU 转录约 0.6s，ONNX 格式、CPU 即可跑，无需显卡），
配合 CT-Transformer 做标点恢复，完全离线运行。若日后需要更高准确率或显卡加速，
可参考原版 README 换用 Qwen3-ASR / SenseVoice 等引擎（对应 sherpa-onnx 亦支持）。

## 模型精简与恢复

2026-08-31 精简：删除了不再使用的模型与测试文件（均移入系统回收站，
可恢复；如需彻底删除请自行清空回收站）。以下记录便于日后恢复。

### 当前使用的模型（`models/`）

| 用途 | 目录 | 文件 | 来源 |
| --- | --- | --- | --- |
| 语音识别（ASR） | `models/paraformer-offline-zh/` | `model.int8.onnx`（int8 量化）+ `tokens.txt` + `am.mvn` | [sherpa-onnx-paraformer-zh-2023-09-14](https://huggingface.co/csukuangfj/sherpa-onnx-paraformer-zh-2023-09-14)（`git clone` 后删除了仓库多余文件） |
| 标点恢复（PUNC） | `models/Punct-CT-Transformer/sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12/` | `model.onnx`（fp32） | [CapsWriter-Offline v1.0 模型包](https://github.com/HaujetZhao/CapsWriter-Offline/releases/tag/v1.0)（或[百度网盘 提取码 eu4c](https://pan.baidu.com/s/1zNHstoWZDJVynCBz2yS9vg)） |

### 已删除内容及恢复方式

| 已删除 | 大小 | 为什么删 | 恢复方式 |
| --- | --- | --- | --- |
| `models/Paraformer/speech_paraformer-large-vad-punc_asr_nat-zh-cn-16k-common-vocab8404-onnx/`（fp32 原版） | 248M | 已被 `paraformer-offline-zh` int8 版取代（快数倍、内存更低，准确率损失极小） | 从 [CapsWriter-Offline models Release](https://github.com/HaujetZhao/CapsWriter-Offline/releases/tag/models) 重新下载；或从[回收站]恢复。恢复后把 `config.yaml` 的 `asr.paraformer_model`/`asr.tokens` 指回其 `model.onnx`/`tokens.txt` |
| `models/punc_ct-transformer_cn-en/`（根目录，FunASR runtime 格式） | 1.0G | **sherpa-onnx 无法加载**（FunASR 格式：`model_quant.onnx` + jieba 词典 + `punc.yaml`，无 sherpa-onnx 所需 tokens 词表），与客户端用的 `vocab272727` 版不是同一套格式 | `git clone https://www.modelscope.cn/damo/punc_ct-transformer_cn-en-common-vocab471067-large-onnx.git`（仅供 FunASR runtime 使用，勿再放入本项目） |
| `models/模型测试/`（根目录） | 13M | 一次性测试脚本（sherpa-onnx websocket 服务端、字幕生成等），与客户端无关 | 仅测试用途，如需可重建（sherpa-onnx 官方示例脚本） |
| `models/模型下载方式.txt`（根目录） | 1K | 下载说明已并入本表 | 见上两行链接 |

### 精简时间线备忘

- 2026-08-31 前：根目录平行存在 `CapsWriter-Offline/`（Python 源码 + 模型）、
  根目录 `models/`（int8 测试模型）、`qt-tray-gui/`、`qt-tray-icon-ui/`。
- 2026-08-31：`CapsWriter-Offline/` 整体移入回收站（备份在
  `.workbuddy/backup/CapsWriter-Offline-20260831/`，542M / 395 文件）；
  其 `models/Paraformer`（fp32）、`models/Punct-CT-Transformer` 与
  `hot-rule.txt` 迁入本项目；`qt-tray-gui/`、`qt-tray-icon-ui/` 已由用户自行删除。
- 2026-08-31（本次）：ASR 切换为 int8 量化版并迁入本项目，删除 fp32 原版
  与根目录 `models/` 全部内容，根目录 `models/` 文件夹已移除。

## 常见问题（FAQ）

以下要点参考自原版 [CapsWriter-Offline](https://github.com/HaujetZhao/CapsWriter-Offline)
README，行为与本项目一致：

- **为什么按了没反应？** 确认 `capswriter.exe` / `capswriter-gui.exe` 进程还在
  运行；若要在以管理员权限运行的程序（如部分编辑器/终端）中输入，本程序也
  需以管理员权限启动。
- **识别结果没字？** 确认麦克风是否录到音（建议桌面 USB 麦克风）、Windows
  麦克风权限是否已开启。程序不保存录音文件，无法事后回放排查。
- **如何开机自启？** `Win+R` 输入 `shell:startup` 打开启动文件夹，放入
  `capswriter-gui.exe` 的快捷方式即可（托盘版无黑窗口，更适合常驻）。
- **平台支持？** 与原版相同，仅保证 Windows 10/11（64 位）下运行。
