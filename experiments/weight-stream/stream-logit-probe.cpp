#include "llama.h"
#include "ggml-backend.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>

int main(int argc, char ** argv) {
    if (argc < 4 || argc > 6) { std::fprintf(stderr, "usage: logit_probe MODEL cpu|gpu OUTPUT [auto|no-flash] [first|long|long512|real128|file:PATH]\n"); return 1; }
    const bool cpu = std::string(argv[2]) == "cpu";
    if (!cpu && std::string(argv[2]) != "gpu") return 1;
    ggml_backend_load_all();
    llama_backend_init();
    auto mp = llama_model_default_params();
    if (std::getenv("MALI_PROBE_DISABLE_REPACK")) mp.use_extra_bufts = false;
    ggml_backend_dev_t no_devices[] = { nullptr };
    llama_model_tensor_buft_override overrides[] = {{nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr}};
    const bool force_all = std::getenv("MALI_PROBE_FORCE_ALL_GPU") != nullptr;
    const bool force_embed = force_all || std::getenv("MALI_PROBE_FORCE_GPU_EMBED") != nullptr;
    const bool force_output = force_all || std::getenv("MALI_PROBE_FORCE_GPU_OUTPUT") != nullptr;
    const bool stream = std::getenv("MALI_PROBE_STREAM_WEIGHTS") != nullptr;
    if (stream) {
        mp.no_host = true;
        mp.use_extra_bufts = false;
        mp.load_mode = LLAMA_LOAD_MODE_MMAP;
    }
    if (!cpu && stream) {
        auto * dev = ggml_backend_dev_by_name("CPU");
        if (!dev) { std::fprintf(stderr, "CPU backend is unavailable\n"); return 11; }
        overrides[0] = {".*", ggml_backend_dev_buffer_type(dev)};
        mp.tensor_buft_overrides = overrides;
    } else if (!cpu && (force_embed || force_output)) {
        auto * dev = ggml_backend_dev_by_name("Vulkan0");
        if (!dev) { std::fprintf(stderr, "Vulkan0 is unavailable\n"); return 11; }
        auto * buft = ggml_backend_dev_buffer_type(dev);
        int i = 0;
        if (force_embed) overrides[i++] = {"^token_embd\\.weight$", buft};
        if (force_output) overrides[i++] = {"^output\\.weight$", buft};
        mp.tensor_buft_overrides = overrides;
    }
    mp.n_gpu_layers = cpu ? 0 : 99;
    if (cpu) mp.devices = no_devices;
    llama_model * model = llama_model_load_from_file(argv[1], mp);
    if (!model) return 2;
    auto cp = llama_context_default_params();
    if (std::getenv("MALI_PROBE_K_CACHE_F32")) cp.type_k = GGML_TYPE_F32;
    const bool realistic = argc == 6 && std::string(argv[5]) == "real128";
    const bool file_input = argc == 6 && std::string(argv[5]).rfind("file:", 0) == 0;
    const bool extended_512 = file_input && std::getenv("MALI_PROBE_EXTENDED_512");
    const bool long_input = argc == 6 && (std::string(argv[5]) == "long" || std::string(argv[5]) == "long512" || realistic || file_input);
    const bool long512 = argc == 6 && std::string(argv[5]) == "long512";
    cp.n_ctx = long512 ? 1024 : 256; cp.n_batch = long512 ? 512 : long_input ? 128 : 32; cp.n_ubatch = file_input ? 32 : long_input ? 128 : 32; cp.n_threads = 4; cp.n_threads_batch = 4;
    // Controlled non-repetitive 128-token experiment: fixed microbatches keep
    // peak compute memory low, and the larger context exercises KV placement.
    if (realistic) {
        cp.n_ctx = 512;
        cp.n_batch = 128;
        cp.n_ubatch = std::getenv("MALI_PROBE_UBATCH_64") ? 64 : 32;
    }
    if (extended_512) { cp.n_ctx = 1024; cp.n_batch = 512; cp.n_ubatch = 32; }
    if (argc >= 5 && std::string(argv[4]) == "no-flash") cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
    if (cpu) { cp.offload_kqv = false; cp.op_offload = false; }
    llama_context * ctx = llama_init_from_model(model, cp);
    if (!ctx) return 3;
    const llama_vocab * vocab = llama_model_get_vocab(model);
    std::string prompt = "The capital of France is Paris. Paris is also the";
    if (argc == 6 && std::string(argv[5]) == "first") prompt = "The capital of France is";
    if (file_input) {
        std::ifstream file(std::string(argv[5]).substr(5), std::ios::binary);
        if (!file) return 10;
        prompt.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    const bool long_prompt = long_input && !file_input;
    if (realistic) {
        prompt = "A small coastal town is planning a public library. The council has a limited budget and wants the building to serve children, elderly residents, and people who work during the day. Some residents prefer a quiet reading room, while others ask for computers, language classes, and a space for community meetings. The proposed site is close to a bus stop but may be affected by flooding during winter storms. Architects have suggested raising the ground floor and using durable materials, but these choices would increase the initial cost. Local volunteers can help with events, although they cannot replace trained staff. The council needs to compare maintenance expenses, opening hours, and accessibility before choosing a design. A survey has produced different answers from different neighborhoods, so a fair decision should consider who responded and who may have been missed. Write a short recommendation that explains the main tradeoffs, suggests what information is still needed, and describes a practical next step for the town. The recommendation should not invent costs or claim that all residents agree. It should explain how the library could adapt to future needs while respecting the available budget. The first priority is";
    } else if (long_prompt) for (int i = 0; i < (long512 ? 80 : 30); i++) prompt += " The quick brown fox jumps over the lazy dog.";
    if (std::getenv("MALI_PROBE_CHAT_FORMAT")) {
        const char * tmpl = llama_model_chat_template(model, nullptr);
        if (!tmpl) { std::fprintf(stderr, "model has no chat template\n"); return 19; }
        const bool use_system = std::getenv("MALI_PROBE_CHAT_SYSTEM") != nullptr;
        const llama_chat_message messages[] = {
            {"system", "You are Qwen, created by Alibaba Cloud. You are a helpful assistant."},
            {"user", prompt.c_str()},
        };
        const llama_chat_message * chat = use_system ? messages : messages + 1;
        const size_t n_messages = use_system ? 2 : 1;
        const int length = llama_chat_apply_template(tmpl, chat, n_messages, true, nullptr, 0);
        if (length < 0) { std::fprintf(stderr, "chat template unsupported\n"); return 20; }
        std::vector<char> formatted(length + 1);
        const int actual = llama_chat_apply_template(tmpl, chat, n_messages, true, formatted.data(), formatted.size());
        if (actual != length) return 21;
        prompt.assign(formatted.data(), length);
        std::fprintf(stderr, "probe_chat_template_applied=1 explicit_system=%d formatted_bytes=%d\n", int(use_system), length);
        std::fprintf(stderr, "formatted_prompt_begin\n%s\nformatted_prompt_end\n", prompt.c_str());
    }
    const int n = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), nullptr, 0, false, true);
    if (n <= 0) return 4;
    std::vector<llama_token> tokens(n);
    if (llama_tokenize(vocab, prompt.c_str(), prompt.size(), tokens.data(), n, false, true) != n) return 4;
    if (long_prompt) { int target = long512 ? 512 : 128; if (n < target) return 4; tokens.resize(target); }
    if (extended_512) {
        if (n < 512) { std::fprintf(stderr, "input has only %d tokens; need 512\n", n); return 4; }
        tokens.resize(512);
    } else if (file_input && n > 128) { std::fprintf(stderr, "input has %d tokens, limit is 128\n", n); return 4; }
    const int batch_size = (int) tokens.size();
    llama_batch_ext * batch = llama_batch_ext_init(ctx);
    for (int i = 0; i < batch_size; i++) {
        int idx = llama_batch_ext_add_token(batch, 0, tokens[i]);
        if (idx < 0) return 5;
        llama_pos pos = i;
        if (!llama_batch_ext_set_pos(batch, idx, &pos)) return 5;
    }
    if (!llama_batch_ext_set_output_logits(batch, batch_size - 1, true)) return 5;
    const auto time_start = std::chrono::steady_clock::now();
    if (llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch) != 0) return 6;
    llama_synchronize(ctx);
    const double compute_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - time_start).count();
    std::fprintf(stderr, "probe_compute_seconds=%.6f tokens=%d tokens_per_second=%.6f\n", compute_seconds, batch_size, batch_size / compute_seconds);
    const int nv = llama_vocab_n_tokens(vocab);
    const float * logits = llama_get_logits_ith(ctx, -1);
    if (!logits) return 7;
    FILE * f = std::fopen(argv[3], "wb");
    if (!f) return 8;
    size_t written = std::fwrite(logits, sizeof(float), nv, f);
    std::fclose(f);
    if (written != (size_t) nv) return 9;
    std::vector<int> indices(nv);
    for (int i = 0; i < nv; i++) indices[i] = i;
    std::partial_sort(indices.begin(), indices.begin() + 5, indices.end(), [logits](int a, int b) { return logits[a] > logits[b]; });
    std::printf("mode=%s prompt_tokens=%d vocab=%d\n", argv[2], batch_size, nv);
    for (int i = 0; i < 5; i++) {
        char piece[128]; int id = indices[i];
        int len = llama_token_to_piece(vocab, id, piece, sizeof(piece) - 1, 0, true);
        if (len < 0) len = 0;
        piece[len] = 0;
        std::printf("rank=%d id=%d logit=%.8f piece=%s\n", i + 1, id, logits[id], piece);
    }
    llama_batch_ext_free(batch);
    if (std::getenv("MALI_PROBE_GENERATE_8")) {
        const int limit = std::getenv("MALI_PROBE_MAX_GENERATE") ? std::atoi(std::getenv("MALI_PROBE_MAX_GENERATE")) : 8;
        if (limit < 1 || limit > 32) { std::fprintf(stderr, "invalid generation limit\n"); return 24; }
        int emitted = 0;
        std::string generated_text;
        const auto generation_start = std::chrono::steady_clock::now();
        for (int step = 0; step < limit; ++step) {
            const auto best = std::max_element(logits, logits + nv);
            const llama_token token = static_cast<llama_token>(best - logits);
            std::printf("generation_step=%d token_id=%d\n", step, token);
            char piece[128];
            const int piece_len = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, true);
            if (piece_len > 0 && piece_len <= static_cast<int>(sizeof(piece))) generated_text.append(piece, piece_len);
            ++emitted;
            if (llama_vocab_is_eog(vocab, token) || step == limit-1) break;
            llama_batch_ext * next = llama_batch_ext_init(ctx);
            if (!next) return 15;
            const int index = llama_batch_ext_add_token(next, 0, token);
            const llama_pos pos = batch_size + step;
            if (index < 0 || !llama_batch_ext_set_pos(next, index, &pos) ||
                !llama_batch_ext_set_output_logits(next, index, true)) return 16;
            if (llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, next) != 0) return 17;
            llama_synchronize(ctx);
            llama_batch_ext_free(next);
            logits = llama_get_logits_ith(ctx, -1);
            if (!logits) return 18;
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - generation_start).count();
        std::printf("generated_text_begin\n%sgenerated_text_end\n", generated_text.c_str());
        std::fprintf(stderr, "generated_tokens=%d generation_seconds=%.6f\n", emitted, elapsed);
    }
    llama_free(ctx); llama_model_free(model); llama_backend_free();
    return 0;
}
