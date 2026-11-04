# IQ3_S 全 GPU 和后台清理实验

**后续更新（2026-11-04）：** 本文记录的是较早的权重常驻 GPU 实验。后来通过文件映射和按需搬运，IQ3_S 已完成模型计算图全部调度至 Vulkan 的生成测试；详细条件和限制见《IQ3按需搬运全GPU实验》。

日期：2026-11-03。测试设备 Redmi K50 / 天玑 8100 / Mali-G610 MC6，Android 14、ARM r32p1。所有新代码、程序和日志保存在隔离实验目录；原模型和原程序未覆盖，系统内存保护保持开启。

## 后台清理

使用 Android `am kill-all` 让系统回收允许结束的后台进程，前台 Termux 保持显示。清理前后系统 MemAvailable 从约 3,463,560 KiB 到 3,498,992 KiB，仅增加约 35 MiB；可用内存还会随后台和缓存变化，不能视作固定容量。

## IQ3_S 的新障碍及实验改动

只读 GGUF 元数据发现，这份 Qwen2.5-7B-Instruct IQ3_S 的 `token_embd.weight` 为 IQ3_S，约 223 MiB；`output.weight` 为 Q6_K，约 426 MiB。后者超过此设备 256 MiB 的 `maxStorageBufferRange`。此前大矩阵实验只覆盖 IQ1_S 的 Q5_K 输出权重，不能直接用于这个 Q6_K 矩阵。

新增独立实验库和默认关闭的 `GGML_VK_EXPERIMENTAL_LARGE_Q6_MAT`，只在 ARM GPU、指定矩阵形状 `(3584, 152064)`、连续存储等限制下放行 Q6_K，利用已有的按输出行分块路径；其余量化类型和形状不自动放行。没有改动原先 IQ1_S 程序库。补丁 `iq3-full-gpu/iq3-large-q6-experiment.patch` 以此前包括 Q5、选择性精度及融合实验的源码为基础，已在隔离副本通过补丁适用性检查，不是生产版补丁。

关闭开关时，该 Q6_K 实际尺寸算子被标记为不支持；开启后标记为支持。使用固定种子生成、分块重复的量化数据，单 token 和 32 token 两种随机输入测试分别各运行两次，均通过原算子工具的 CPU 对照阈值。这是受限算子测试，不证明所有权重、矩阵尺寸或整个模型的数值准确性。测试补丁为 `iq3-full-gpu/iq3-q6-test-experiment.patch`。

## IQ3_S 全 GPU 试运行

在上述实验库中强制词嵌入和输出权重到 Vulkan0，禁用 Flash Attention，以 5 token 英文提示尝试 8 token 生成。请求上下文 128，程序实际使用 256；物理批次 16。日志记录：

- 词嵌入和 Q6_K 输出权重均放到 Vulkan0，29/29 层卸载；Vulkan 模型缓冲约 3331.41 MiB。
- 预留计算图只有一个 Vulkan0 分段，实际输入图也记录为 Vulkan0 分段；日志到达 `n_past = 5`。
- 进程在生成第一个 token 前被系统终止，退出码 137。因此 IQ3_S 全 GPU 的连续生成仍未成功。

系统日志将本次终止归因于 GPU 相关内存超限：监测约 3,491,656 KiB，记录的当时进程阈值为 1,572,864 KiB。监控口径和阈值可能动态变化，不能用它推断所有配置的绝对限制。运行中一次 MemAvailable 采样约 489,600 KiB；系统低内存回收还终止了若干后台系统服务。测试后设备仍连接，Termux 为前台，可用内存恢复到约 3.9 GiB。

由于相同配置已触发明确的保护且清后台收益很小，本次未反复运行高内存配置。再次尝试需要先实质性降低内存使用，不能通过关闭、绕过或修改系统保护来解决。

## IQ1_S 四次全 GPU 对照

在此前已验证的 IQ1_S 版本上，使用两个不同英文提示、每个提示重复两次，每次贪心生成 8 个 token。四次均正常完成；输入和逐 token 计算日志记录 Vulkan0 分段。每个提示的两次完整前缀词表分数逐位一致，8 个 token 也一致。

其中一条代码提示生成了 ` def add(a b):return def add`，不是有效 Python；该测试没有 CPU 回答质量对照，不能将其原因归结为 GPU。两条提示和短续写不能证明所有输入稳定或准确。

## 文件

- `iq3-full-gpu/iq3-full-attempt1.log`、`iq3-attempt1-memory.tsv`、`iq3-attempt1-system-events.txt`：IQ3 试运行和系统记录；系统日志包含设备及应用信息，对外分享前应检查。
- `iq3-full-gpu/q6-support-*.log`、`q6-test-*.log`：开关支持情况和算子对照。
- `iq3-full-gpu/iq1-controls/`：IQ1 四次短生成、前缀分数和日志。
- 手机独立库位于 `~/mali-clean-verify-20261102-lf/iq3-q6-experiment/`；研究代码与补丁只适合继续验证，不是通用安装包。
