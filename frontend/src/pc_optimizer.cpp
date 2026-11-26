#include "pc_optimizer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fs = std::filesystem;

extern "C" const unsigned char ph_clang_pack[];  // clang_pack.S
extern "C" const unsigned char ph_clang_pack_end[];

namespace ph {

bool pc_opt_available() { return kClangPackCount > 0; }
fs::path pc_opt_dir(const fs::path& data_dir) { return data_dir / "opt"; }

namespace {

std::wstring quote(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

// Runs a command line inside `job` (ended with the program), its output to `log`; returns the exit code.
int run(const std::wstring& cmd, const fs::path& log, HANDLE job) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE out = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = out;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring line = cmd;
    const BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                   nullptr, nullptr, &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) return -1;
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return int(code);
}

std::string tail(const fs::path& log) {
    std::ifstream f(log, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s.size() > 1500 ? s.substr(s.size() - 1500) : s;
}

}  // namespace

std::string pc_clang_build(const fs::path& data_dir, const fs::path& out_module, std::atomic<int>& progress) {
    std::error_code ec;
    const fs::path opt = pc_opt_dir(data_dir), gen = opt / "gen", obj = opt / "obj", tools = opt / "clang";
    std::ofstream log(data_dir / "opt-build.log", std::ios::app);
    progress = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

    // clang, lld and their DLLs out of the exe
    fs::create_directories(tools, ec);
    for (int i = 0; i < kClangPackCount; i++) {
        const ClangPackEntry& e = kClangPack[i];
        std::ofstream f(tools / e.name, std::ios::binary);
        f.write(reinterpret_cast<const char*>(ph_clang_pack + e.offset), std::streamsize(e.size));
        if (!f) return "cannot unpack " + std::string(e.name) + " to " + tools.string();
    }

    struct Src { fs::path path; uintmax_t size; };
    std::vector<Src> sources;
    uintmax_t total = 0;
    for (const auto& e : fs::directory_iterator(gen, ec))
        if (e.path().extension() == ".c") sources.push_back({e.path(), e.file_size(ec)}), total += sources.back().size;
    if (sources.empty()) return "no C to compile in " + gen.string();
    sources.push_back({tools / "chkstk_ms.S", 1}), sources.push_back({tools / "dllstart.c", 1});
    // Biggest first, so the last files to finish are small ones.
    std::sort(sources.begin(), sources.end(), [](const Src& a, const Src& b) { return a.size > b.size; });
    fs::create_directories(obj, ec);

    // A job object: the compilers end with the program.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    // Every thread, up to 16 (the player waits for this build); about 0.5 GB of memory each at most.
    unsigned jobs = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof mem;
    if (GlobalMemoryStatusEx(&mem)) jobs = unsigned(std::max<DWORDLONG>(1, std::min<DWORDLONG>(jobs, mem.ullAvailPhys / (600ull << 20))));
    log << "clang build: " << sources.size() << " files, " << (total >> 20) << " MB of C, " << jobs << " at once, -O2\n";
    std::atomic<size_t> next{0};
    std::atomic<uintmax_t> done{0};
    std::mutex m;
    std::string error;
    std::vector<std::thread> pool;
    for (unsigned j = 0; j < jobs; j++)
        pool.emplace_back([&] {
            for (size_t i; (i = next++) < sources.size();) {
                {
                    std::lock_guard<std::mutex> l(m);
                    if (!error.empty()) return;
                }
                const Src& s = sources[i];
                const fs::path o = obj / (s.path.stem().wstring() + L".o"), tmp = obj / (s.path.stem().wstring() + L".o.tmp");
                const std::wstring cmd = quote(tools / "clang.exe") +
                    L" --target=x86_64-w64-windows-gnu -O2 -w -c -ffreestanding -nostdinc -isystem " + quote(opt / "inc") +
                    L" -I" + quote(gen) + L" -I" + quote(opt / "rt") + L" -DNTR_GAME_MODULE=1 " + quote(s.path) +
                    L" -o " + quote(tmp);
                const fs::path lg = obj / (s.path.stem().wstring() + L".log");
                std::error_code rec;
                if (run(cmd, lg, job) != 0 || (fs::rename(tmp, o, rec), rec)) {
                    std::lock_guard<std::mutex> l(m);
                    if (error.empty()) error = "compiling " + s.path.filename().string() + " failed:\n" + tail(lg);
                    return;
                }
                progress = int(95 * (done += s.size) / std::max<uintmax_t>(1, total));
            }
        });
    for (auto& t : pool) t.join();
    const double compiled = secs();
    if (error.empty()) {
        const fs::path tmp = out_module.wstring() + L".tmp";
        std::wstring cmd = quote(tools / "ld.lld.exe") + L" -m i386pep --shared -e _dllstart --no-insert-timestamp -o " +
                           quote(tmp);
        for (const auto& e : fs::directory_iterator(obj, ec))
            if (e.path().extension() == ".o") cmd += L" " + quote(e.path());
        cmd += L" " + quote(tools / "libmsvcrt.a");  // memcpy and friends, from msvcrt.dll like the TinyCC build
        const fs::path lg = opt / "link.log";
        fs::create_directories(out_module.parent_path(), ec);
        if (run(cmd, lg, job) != 0) {
            error = "linking failed:\n" + tail(lg);
        } else {
            fs::remove(out_module, ec);
            fs::rename(tmp, out_module, ec);
            if (ec) error = "cannot store " + out_module.string() + ": " + ec.message();
        }
    }
    if (job) CloseHandle(job);
    if (error.empty()) {
        char line[160];
        std::snprintf(line, sizeof line, "done: compile %.1f s, link %.1f s, %ju MB\n", compiled, secs() - compiled,
                      uintmax_t(fs::file_size(out_module, ec) >> 20));
        log << line;
        std::fprintf(stderr, "clang build %s", line);
    } else {
        log << "failed: " << error << "\n";
        std::fprintf(stderr, "clang build failed: %s\n", error.c_str());
    }
    fs::remove_all(opt, ec);
    progress = 100;
    return error;
}

}  // namespace ph
