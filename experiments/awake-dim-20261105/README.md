# Mali 推理速度差：电源与 CPU 控制变量实验

日期：2026-11-05。设备为 Redmi K50 / 天玑 8100 / Mali-G610 MC6 / Android 14；模型为 Qwen2.5-7B IQ3_S。每组运行同一 5 token 英文提示、生成 8 token；首 token 从输入阶段取得，生成计时包含 7 次后续计算以及调度、等待和采样。GPU 驻留输出层、词嵌入和前八层 FFN，其余权重按需搬运。

每项对照各运行两次，顺序为 A、B、B、A，切换后等待 20 秒。下表为生成阶段秒数，均值仅用于描述这两次测试，样本量不足以给出稳定设备基准：

| 固定条件 | 对照 A | A 的两次耗时 | 对照 B | B 的两次耗时 |
|---|---|---:|---|---:|
| 手机均为待机 | 插电常亮关闭 | 24.71、23.06（均值 23.88） | 插电常亮打开 | 23.32、23.85（均值 23.58） |
| 手机均为待机，常亮关闭 | 默认 CPU 核心范围 | 22.94、23.72（均值 23.33） | 限定 CPU 4–7 | 22.98、22.19（均值 22.58） |
| 手机均为待机，常亮关闭 | 不持有 CPU 唤醒锁 | 22.83、22.97（均值 22.90） | 持有 CPU 唤醒锁 | 23.63、22.79（均值 23.21） |
| 插电常亮设置均打开 | 手机待机 | 23.87、24.04（均值 23.95） | 手机唤醒 | 15.11、14.79（均值 14.95） |
| 插电常亮设置均打开 | 手机待机、屏幕亮度为 0 | 25.00、23.54（均值 24.27） | 唤醒后自动调到最低亮度 | 14.94、14.84（均值 14.89） |

前两组表明在这几次息屏测试中，仅切换插电常亮设置，或仅将进程限定在 CPU 4–7，均未出现接近上次亮屏与息屏的明显速度差。CPU 范围通过读取进程线程状态验证：默认组为 0–7，大核组为 4–7。频率、线程调度和后台活动未严格固定，不能证明这些因素在所有情况下都无影响。

唤醒锁组使用 Android 调试账户已有权限，运行临时测试进程；两种条件均启动相同辅助进程，但只有一组获取部分唤醒锁。每次均通过系统电源状态采样验证锁的有无，并在结束后释放。持锁条件没有观察到明显加速。Android 官方说明，部分唤醒锁的目的是在显示关闭时保持 CPU 运行；这本身不保证提高运行频率或推理速度。[Android 开发者文档](https://developer.android.com/topic/performance/vitals/stuck-wakelock)

后两组将插电常亮设置固定为打开，仅切换唤醒与待机状态；电源状态在各次运行中定期采样。低亮度组唤醒后等待锁屏界面自动调暗，系统报告实际归一化亮度约 0.00238，保持大于零；保存的用户亮度设置保持不变。它是极暗但仍开启的屏幕，不是完全关屏，测试也没有测量电耗。结果支持上次观察到的速度差与整体交互／电源状态有关，但切换状态可能同时影响多个系统策略，因此不能仅凭本实验断定具体是 GPU 频率、CPU 调度或驱动导致。[Android PowerManager 文档](https://developer.android.com/reference/android/os/PowerManager#isInteractive()) 描述了交互、待机状态与屏幕的关系。

中文长回答另做一次待机和一次极暗唤醒对照：64 token 输入，生成 32 token，生成阶段分别为 101.92 和 64.28 秒；实际执行 31 次后续计算。两次首位置分数及 32 个贪心 token 一致；各一次不能说明所有中文输入都能获得相同提升。数据见 [measurements-chat32.tsv](measurements-chat32.tsv)。

[awake-dim-run.py](awake-dim-run.py) 是实验辅助工具：USB 连接时临时保持手机唤醒，等待系统自行调暗后运行指定的电脑命令，并在结束时恢复原插电常亮设置。工具已分别包裹本机测试脚本和实际 Termux 推理命令跑通，首位置分数与原始测试逐字节一致；自动调暗行为可能因设备、锁屏策略和设置而不同。

20 次运行的首位置 152064 个词表分数二进制文件逐字节相同，8 个贪心 token 全部相同；调度日志记录的计算图分段均为 Vulkan0。CPU 仍负责权重搬运、调度、分词和采样。测试只覆盖一个短提示、一台手机，不代表所有模型、输入和设备的性能或精度。

最低可用内存采样是 2,531,596 KiB；`MemAvailable` 含可回收文件缓存，间隔采样不能捕捉全部瞬时变化。各组结束时临时唤醒锁均已释放，插电常亮设置恢复原值。系统保护、模型文件和默认启动方式没有修改。

完整数据摘要为 [measurements-short.tsv](measurements-short.tsv)，状态采样见 [power-samples.tsv](power-samples.tsv)；完整原始日志留在实验机本地，未纳入本精选发布。本实验是单机实验性的电源状态方案，无法证明普适性或功耗收益。

## 运行极暗唤醒辅助工具

此工具在电脑上运行，需要 Python 3、ADB、USB 连接及此前可用的模型和实验版 llama.cpp。它临时设置“USB 连接时保持唤醒”，等待设备自己调暗后运行给定的**电脑命令**，最后恢复原来的插电常亮设置及初始待机状态；它不会把屏幕完全关闭，也不会修改屏幕亮度设置。若 60 秒内系统没有达到工具检测的极暗唤醒状态，则不启动推理并恢复设置。长时间运行的电耗、发热和面板影响未在此测量。

需要先按[前一份实验报告](../screen-state-20261105/README.md)编译探针并把该目录的 `run-one-termux.sh`、提示文件和探针放到手机上。下面是电脑端类 Unix shell 的示例，路径均需按实际位置调整；其他系统可用等价方式传入相同参数：

```bash
ADB=adb
BUILD=/data/data/com.termux/files/home/llama-src/build/bin
LIB=/data/data/com.termux/files/home/mali-vulkan
REPRO=/data/data/com.termux/files/home/mali-exp/screen-state-20261105
MODEL=/data/data/com.termux/files/home/models/Qwen2.5-7B-Instruct.i1-IQ3_S.gguf
python awake-dim-run.py -- "$ADB" shell run-as com.termux env \
  "MALI_LLAMA_BUILD=$BUILD" "MALI_VULKAN_LIB=$LIB" \
  /data/data/com.termux/files/usr/bin/bash "$REPRO/run-one-termux.sh" \
  short "$MODEL" "$REPRO/resident-ffn8-probe" "$REPRO/short-logits.bin"
```

将 `short` 改为 `chat32` 可使用对应中文提示。可以通过 `--adb` 指定电脑上 ADB 的路径，或用 `--serial` 选择设备；指定设备时，工具会把 `ANDROID_SERIAL` 传给子进程，子命令显式指定的设备仍须与之相同。`--` 后第一个参数仍需是实际可执行的电脑端命令。辅助工具本身不提供模型内存保护；示例调用的 `run-one-termux.sh` 包含实验用内存保护。工具不能保证中断任意外部命令后，其启动的远程进程一定已退出，应由所用推理程序处理自身停止与清理。

Termux 自带的 `termux-wake-lock` 在本机测试时未能启动其对应服务。为避免把未获得的锁误当成有效测试，唤醒锁控制组使用 [MaliCpuWakeProbe.java](MaliCpuWakeProbe.java)，经 Android 调试账户持有有时限的标准部分唤醒锁，并从系统电源状态中核对。它是仅用于诊断的临时程序，依赖 Android 内部初始化接口，不能当成通用应用方案。
