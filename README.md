# llama.cpp on Mali-G610: experiments and evidence

在 Redmi K50（天玑 8100、Mali-G610 MC6）上研究 llama.cpp 的 Vulkan 兼容性、模型计算放置、数值误差和推理速度。本仓库保存关键补丁、测试日志、对照数据及实验报告，供复现和继续研究。

**结果截至 2026-11-04，仅在一台设备上测试。** 实验未修改系统驱动，不需要 Root、解锁或刷机。实验方案不是官方支持的旧 Mali 修复，也不是生产环境实现。

## 已观察到的结果

| 实验 | 实测结果 | 局限 |
| --- | --- | --- |
| Vulkan 队列 | 普通队列上 `vkGetDeviceQueue2` 得到空句柄；`vkGetDeviceQueue` 得到可提交的句柄 | 不能由此断定问题属于驱动还是 loader，也不能推广至其他设备 |
| IQ1_S 全 GPU 模型计算 | Qwen2.5-7B IQ1_S 的嵌入、模型层和输出计算可调度到 Vulkan | CPU 仍负责加载、调度、分词和采样；长文本质量与稳定性未充分验证 |
| IQ1_S 输出层优化 | 单次 32 token 命令行测试中，生成阶段约从 0.56 提升到 1.10 token/秒 | 每种配置仅一次，输出后半存在差异，频率和后台状态未固定 |
| IQ3_S 按需搬运 | 权重由 CPU 文件映射，按需上传到 Vulkan；英文续写 16 token，中文对话 32 token；记录的计算图分段均为 Vulkan0 | 权重不常驻 GPU；CPU 仍参与数据搬运和调度；仅一台设备、少量输入 |
| IQ3_S 中文对话 | 32 token 启动器测试中生成“他还剩4本”，退出码 0 | 单一样本，不能证明模型一般能力或 GPU 数值正确性 |
| IQ3_S 性能 | 同一中文启动器测试，输入处理约 0.80 token/秒，后续生成约 0.26 token/秒 | 单次观察，按需搬运较慢，不能推广为设备的固定速度 |

另有一组相同提示的 IQ3 CPU/GPU 探针对照：前 18 个 token 一致，随后出现差异；两者均在 32 token 截断前得到“四本”，但该探针的回答尚未完整结束。首位置 152064 个词表分数有可观察差异，不能仅凭正确的算术结果断言模型精度。

## 文件和证据

- [完整实验进展汇总](experiments/Mali实验成果总览.md)和[IQ3 按需搬运实验报告](experiments/IQ3按需搬运全GPU实验.md)。
- [IQ1 大矩阵优化](experiments/全GPU生成加速实验报告.md)、[数值误差研究](experiments/Mali数值差异与模型回答复测.md)、[512 token 对照](experiments/512token批次与续写复测报告.md)。
- [IQ3 中文对话日志](experiments/weight-stream/launcher-chat-iq3-32.log)、[内存采样](experiments/weight-stream/launcher-chat-iq3-32-memory.tsv)、[CPU/GPU 探针日志和数据](experiments/weight-stream/)、[数据摘要](experiments/weight-stream/summary.txt)。
- [IQ1 优化前日志](experiments/large-matvec/full-model/cli32-baseline.log)和[优化后日志](experiments/large-matvec/full-model/cli32-optimized.log)。
- [独立 Vulkan 队列测试源码](experiments/queue-probe.c)及[结果](experiments/queue-probe-legacy.txt)。
- [文件清单和 SHA-256](DATA-MANIFEST.json)。较早报告可能引用未纳入精选集的日志或文件；这些历史引用不代表本仓库包含完整原始实验目录。

在仓库根目录执行 `python verify-data.py` 可以检查精选实验文件与清单中的 SHA-256 是否一致。

`.bin` 文件是用于对照的单次词表分数，不包含模型权重。模型 GGUF、驱动库、大型中间张量、重复日志和原始系统事件未收入本仓库。文件清单中的哈希针对本仓库发布副本；日志中的本机标识如存在已作脱敏。

## 内存、风险和结果边界

IQ3 文件映射实验中，模型权重约 3331 MiB；一次启动器测试的 Vulkan 计算缓冲约 431 MiB、上下文约 14 MiB。中文启动器测试每两秒采样的最低 `MemAvailable` 为 3,620,064 KiB，其中包括可回收文件缓存，并非同等大小的空闲物理内存。启动器在采样值低于 700,000 KiB 时向推理进程发送停止信号；它无法保证捕获所有瞬时内存压力。

先前未使用文件映射的一次 IQ3 测试曾使可用内存显著下降，Android 系统终止了 Termux 及其他后台进程；后续不推荐重复该配置。持续运行仍可能导致内存回收、应用退出、发热或电量消耗。不要关闭系统保护来复现。

## 复现补丁链

本地归档显示最接近的上游基线是 [llama.cpp `dde879bc4f954b809892fd0d660515f42536394a`](https://github.com/ggml-org/llama.cpp/commit/dde879bc4f954b809892fd0d660515f42536394a)。原手机源码没有完整 Git 历史，因此不能确认整个测试源码树与该提交完全相同；我们仅验证了下面的 **Vulkan 源文件补丁链**。

从仓库根目录执行，使用独立副本并检查每个补丁：

```sh
git clone https://github.com/ggml-org/llama.cpp.git llama-src
git -C llama-src checkout dde879bc4f954b809892fd0d660515f42536394a
for patch in \
  prompt-suite/upstream-to-experiment.patch \
  gpu-placement/large-q5-experiment.patch \
  layer-trace/selective-fp32-experiment.patch \
  fusion-investigation/fusion-investigation.patch \
  iq3-full-gpu/iq3-large-q6-experiment.patch \
  iq3-real-q6/q6-selective-fp32.patch \
  large-matvec/large-matvec-experiment.patch
do
  git -C llama-src apply --check "../experiments/$patch" || exit 1
  git -C llama-src apply "../experiments/$patch" || exit 1
done
```

上述顺序在独立临时副本中通过 `git apply` 检查。所得 `ggml/src/ggml-vulkan/ggml-vulkan.cpp` 与保留的测试文件在统一 LF 换行后逐字节一致，其 SHA-256 为 `39a35e20e5978c1359da57b962d9c031b2e53a1caedb3971f894789823a8dfdc`。这不是完整构建的逐文件复现证明。依赖版本见[记录](experiments/prompt-suite/dependency-versions.txt)。

实验环境使用 Android Termux、Clang、CMake、Ninja 和 Vulkan 开发工具，需自行配置对应构建依赖及合法获取模型。下面是构建方向示例，**本次发布没有在全新环境验证这组命令**：

```sh
cmake -S llama-src -B llama-src/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGGML_VULKAN=ON
cmake --build llama-src/build
```

`experiments/mali-llama-stream.sh` 记录了测试配置和内存监测，默认依赖实验机的构建目录。复制使用前需要配置 `MALI_LLAMA_BUILD` 和 `MALI_MATVEC_LIBRARY_DIR`，并确认对应目录的可执行文件及实验 Vulkan 共享库存在。它不是独立安装包。测试示例和限制见[IQ3 报告](experiments/IQ3按需搬运全GPU实验.md)。

## 后续工作

扩大模型和提示覆盖范围，验证更长生成；建立多轮 CPU/GPU 和高精度参考对照；降低每 token 搬运开销；在其他 Mali 设备复查队列行为和结果。欢迎提交可复现数据、修正或不同设备的测试结果。

## 许可与第三方材料

本仓库新增的实验说明、脚本及原创改动按 [MIT License](LICENSE) 发布。补丁包含来自 llama.cpp 的上下文和衍生代码，仍须保留 [llama.cpp 的 MIT 许可证及版权声明](third_party/llama.cpp-LICENSE)。模型及其权重不包含在仓库内；使用者须自行遵守模型许可。日志中的硬件和软件名称归其各自权利人所有。
