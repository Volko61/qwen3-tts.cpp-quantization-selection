#include "qwen3_tts.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool read_line(std::string & line) {
    line.clear();

    int ch;
    while ((ch = std::getc(stdin)) != EOF) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            return true;
        }
        line.push_back((char) ch);
    }

    return !line.empty();
}

bool decode_hex_text(const std::string & hex, std::string & out, std::string & error) {
    if (hex.size() % 2 != 0) {
        error = "hex text payload has odd length";
        return false;
    }

    out.clear();
    out.reserve(hex.size() / 2);

    auto hex_value = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
        return -1;
    };

    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hex_value(hex[i]);
        const int lo = hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            error = "hex text payload contains invalid characters";
            return false;
        }
        out.push_back((char) ((hi << 4) | lo));
    }

    return true;
}

int language_to_id(const std::string & language) {
    if (language == "en" || language == "english") return 2050;
    if (language == "ru" || language == "russian") return 2069;
    if (language == "zh" || language == "chinese") return 2055;
    if (language == "ja" || language == "japanese") return 2058;
    if (language == "ko" || language == "korean") return 2064;
    if (language == "de" || language == "german") return 2053;
    if (language == "fr" || language == "french") return 2061;
    if (language == "es" || language == "spanish") return 2054;
    if (language == "it" || language == "italian") return 2070;
    if (language == "pt" || language == "portuguese") return 2071;
    return -1;
}

void print_usage(const char * program) {
    std::fprintf(stderr, "Usage: %s [options] -m <model_dir>\n", program);
    std::fprintf(stderr, "\n");
    std::fprintf(stderr, "Options:\n");
    std::fprintf(stderr, "  -m, --model <dir>         Model directory (required)\n");
    std::fprintf(stderr, "  --tts-model <file>        Explicit TTS model GGUF\n");
    std::fprintf(stderr, "  --tokenizer-model <file>  Explicit tokenizer/vocoder GGUF\n");
    std::fprintf(stderr, "  -j, --threads <n>         Number of threads (default: 4)\n");
    std::fprintf(stderr, "  --max-tokens <n>          Maximum audio tokens (default: 4096)\n");
    std::fprintf(stderr, "  --temperature <val>       Sampling temperature (default: 0.9)\n");
    std::fprintf(stderr, "  --top-k <n>               Top-k sampling (default: 50)\n");
    std::fprintf(stderr, "  --repetition-penalty <v>  Repetition penalty (default: 1.05)\n");
    std::fprintf(stderr, "  -h, --help                Show this help\n");
    std::fprintf(stderr, "\n");
    std::fprintf(stderr, "Protocol on stdin:\n");
    std::fprintf(stderr, "  SYNTH\\n<language>\\n<output_path>\\n<hex_utf8_text>\\n\n");
    std::fprintf(stderr, "  QUIT\\n\n");
}

bool save_audio_to_path(const std::string & output_path, const qwen3_tts::tts_result & result, std::string & error) {
    if (!qwen3_tts::save_audio_file(output_path, result.audio, result.sample_rate)) {
        error = "failed to save output file";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char ** argv) {
    std::string model_dir;
    std::string explicit_tts_model;
    std::string explicit_tokenizer_model;

    qwen3_tts::tts_params params;
    params.print_progress = false;
    params.print_timing = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-m" || arg == "--model") {
            if (++i >= argc) {
                std::fprintf(stderr, "Error: missing model directory\n");
                return 1;
            }
            model_dir = argv[i];
        } else if (arg == "--tts-model") {
            if (++i >= argc) {
                std::fprintf(stderr, "Error: missing tts model path\n");
                return 1;
            }
            explicit_tts_model = argv[i];
        } else if (arg == "--tokenizer-model") {
            if (++i >= argc) {
                std::fprintf(stderr, "Error: missing tokenizer model path\n");
                return 1;
            }
            explicit_tokenizer_model = argv[i];
        } else if (arg == "-j" || arg == "--threads") {
            if (++i >= argc) {
                std::fprintf(stderr, "Error: missing threads value\n");
                return 1;
            }
            params.n_threads = std::stoi(argv[i]);
        } else if (arg == "--max-tokens") {
            if (++i >= argc) {
                std::fprintf(stderr, "Error: missing max-tokens value\n");
                return 1;
            }
            params.max_audio_tokens = std::stoi(argv[i]);
        } else if (arg == "--temperature") {
            if (++i >= argc) {
                std::fprintf(stderr, "Error: missing temperature value\n");
                return 1;
            }
            params.temperature = std::stof(argv[i]);
        } else if (arg == "--top-k") {
            if (++i >= argc) {
                std::fprintf(stderr, "Error: missing top-k value\n");
                return 1;
            }
            params.top_k = std::stoi(argv[i]);
        } else if (arg == "--repetition-penalty") {
            if (++i >= argc) {
                std::fprintf(stderr, "Error: missing repetition-penalty value\n");
                return 1;
            }
            params.repetition_penalty = std::stof(argv[i]);
        } else {
            std::fprintf(stderr, "Error: unknown argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (model_dir.empty()) {
        std::fprintf(stderr, "Error: model directory is required\n");
        print_usage(argv[0]);
        return 1;
    }

    qwen3_tts::Qwen3TTS tts;
    if (!tts.load_models(model_dir, explicit_tts_model, explicit_tokenizer_model)) {
        std::printf("ERR\n%s\n", tts.get_error().c_str());
        std::fflush(stdout);
        return 1;
    }

    std::printf("READY\n");
    std::fflush(stdout);

    std::string command;
    while (read_line(command)) {
        if (command.empty()) {
            continue;
        }

        if (command == "QUIT") {
            std::printf("BYE\n");
            std::fflush(stdout);
            return 0;
        }

        if (command != "SYNTH") {
            std::printf("ERR\nunknown command\n");
            std::fflush(stdout);
            continue;
        }

        std::string language;
        std::string output_path;
        std::string text_hex;
        if (!read_line(language) || !read_line(output_path) || !read_line(text_hex)) {
            std::printf("ERR\nincomplete request\n");
            std::fflush(stdout);
            return 1;
        }

        std::string text;
        std::string decode_error;
        if (!decode_hex_text(text_hex, text, decode_error)) {
            std::printf("ERR\n%s\n", decode_error.c_str());
            std::fflush(stdout);
            continue;
        }

        const int language_id = language_to_id(language);
        if (language_id < 0) {
            std::printf("ERR\nunsupported language\n");
            std::fflush(stdout);
            continue;
        }

        params.language_id = language_id;
        qwen3_tts::tts_result result = tts.synthesize(text, params);
        if (!result.success) {
            std::printf("ERR\n%s\n", result.error_msg.c_str());
            std::fflush(stdout);
            continue;
        }

        std::string save_error;
        if (!save_audio_to_path(output_path, result, save_error)) {
            std::printf("ERR\n%s\n", save_error.c_str());
            std::fflush(stdout);
            continue;
        }

        std::printf("OK\n%lld\n", (long long) result.t_total_ms);
        std::fflush(stdout);
    }

    return 0;
}