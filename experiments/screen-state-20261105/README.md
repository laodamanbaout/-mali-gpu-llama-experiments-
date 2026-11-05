# Mali-G610 上的屏幕电源状态与 llama.cpp 推理速度

日期：2026-11-05。**单台 Redmi K50 / 天玑 8100 / Mali-G610 MC6 / Android 14，结果是设备上的实验观察。** 在同一个 Qwen2.5-7B IQ3_S 模型及同一 GPU 权重驻留配置下，亮屏并保持 USB 唤醒的测试比息屏待机测试更快；屏幕状态和保持唤醒设置同时变化，不能把差异单独归因于屏幕、GPU 驱动或某个频率机制。

| 测试 | 息屏 / Dozing | 亮屏 / Awake | 范围 |
|---|---|---|---|
| 5 token 英文输入，生成 8 token | 30.73, 21.30, 20.59, 22.57 秒；平均 23.80 秒 | 16.15, 18.42, 15.61, 14.68 秒；平均 16.21 秒 | 各四次，首生成 token 来自输入处理，生成阶段实际执行 7 次后续模型计算 |
| 64 token 中文对话输入，生成 32 token | 102.57 秒 | 67.35 秒 | 各一次，生成阶段执行 31 次后续模型计算 |

短输入组亮屏平均生成耗时低约 31.9%，相当于按 7 次后续计算除以平均时间计算的吞吐由 0.294 提高到 0.432 次/秒。中文组只有各一次，不足以单独证明稳定加速。生成阶段计时包含调度、等待和采样；不含输入处理，不能当作纯 GPU 耗时。

两组短输入分别采用“息屏、亮屏、亮屏、息屏”的顺序；第二组每次切换后等待 20 秒。中文对照按息屏、亮屏顺序，每次切换后等待 20 秒。试验中对 `mWakefulness` 和 `mStayOn` 定期采样，结果见 [power-samples.tsv](power-samples.tsv)；息屏为 Dozing 且 stay-on false，亮屏为 Awake 且 stay-on true。样本有间隔，不能排除短时状态变化；频率、缓存、后台活动、温度等因素未完全固定。

相同提示下，所有短输入运行的首位置 152064 个词表分数二进制文件完全相同，8 个贪心 token 完全相同；中文对照首位置分数和 32 个 token 也分别相同。每次记录到的模型图分段均为 Vulkan0。CPU 仍负责权重存储、搬运、调度、分词和采样；这不意味着 CPU 完全不参与推理。

## 配置及证据

模型为 Qwen2.5-7B-Instruct IQ3_S。GPU 常驻输出层、词嵌入和前八层 FFN 的 gate/up/down 权重，其他权重由 CPU 文件映射、按需搬运；GPU 模型缓冲约 1317.42 MiB。两种模式使用相同实验库、无 flash attention、4 CPU 线程、贪心选择。具体提示见 [英文](prompt-english.txt) 与 [中文](prompt-chinese.txt)，测量和内存采样摘要见 [measurements.tsv](measurements.tsv)。其中 SHA-256 是本机首位置分数二进制数据的哈希；模型、分数二进制文件及完整原始日志未包含在本精选发布中。

测试开始前要求系统 MemAvailable 至少 3,300,000 KiB，采样低于 1,800,000 KiB 时停止测试。可用内存包含可回收文件缓存，采样会漏掉短暂波动。控制器在实验后恢复了原 `stay_on_while_plugged_in` 值与息屏状态。

## 复现方法与限制

本测试依赖本仓库主 README 所列的实验补丁链和构建；由于原测试源码不是完整可验证的上游快照，不能保证在其他版本直接构建。另需自行取得兼容模型。[resident-ffn8-probe.cpp](resident-ffn8-probe.cpp) 与手机测试源码的 SHA-256 一致；[run-one-termux.sh](run-one-termux.sh) 是带内存监控的独立辅助脚本，在同一手机上用英文短提示验证过，其他设备及全新构建尚未验证。以下路径由使用者自行设置；`libllama.so.0.8.0` 是本机实际构建文件名，其他版本可能不同：

```bash
export LLAMA_SOURCE=/path/to/llama-source
export MALI_LLAMA_BUILD="$LLAMA_SOURCE/build/bin"
export MALI_VULKAN_LIB=/path/to/experimental-vulkan-library
export MODEL=/path/to/Qwen2.5-7B-Instruct-IQ3_S.gguf
clang++ -std=c++17 -O2 -I"$LLAMA_SOURCE/include" -I"$LLAMA_SOURCE/ggml/include" \
  resident-ffn8-probe.cpp -L"$MALI_VULKAN_LIB" -L"$MALI_LLAMA_BUILD" \
  -Wl,--no-as-needed "$MALI_LLAMA_BUILD/libllama.so.0.8.0" \
  -lggml -lggml-cpu -lggml-vulkan -lggml-base -o resident-ffn8-probe
bash run-one-termux.sh short "$MODEL" ./resident-ffn8-probe short-logits.bin
bash run-one-termux.sh chat32 "$MODEL" ./resident-ffn8-probe chat-logits.bin
```

每次运行前应确认使用的库包含仓库所述实验改动。辅助脚本不会切换屏幕状态；应自行设置与记录条件，且不应覆盖原模型。原始设备验证与跨设备验证是两件不同的事。

实验设备通过 USB 供电。切换条件时，息屏使用 Android 睡眠键并关闭插电常亮；亮屏唤醒设备并启用 USB 插电常亮。实验者应记录并在结束时恢复原设置。其他手机、系统、模型、输入和更长回答可能表现不同；本结果不是通用的 Mali 优化，也未证明 GPU 频率变化是原因。
