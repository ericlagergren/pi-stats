#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

extern "C" {
#include <getopt.h>
#include <unistd.h>

#include <bcm_host.h>
#include <interface/vmcs_host/vc_vchi_gencmd.h>
}

void fatal(const std::string& msg) {
    std::cerr << msg << std::endl;
    std::exit(EXIT_FAILURE);
}

bool has_prefix(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.length(), prefix) == 0;
}

bool has_prefix(const std::string& s, const char prefix) {
    return s.length() > 0 && s.at(0) == prefix;
}

void trim_prefix(std::string& s, const std::string& prefix) {
    if (has_prefix(s, prefix)) {
        s.erase(0, prefix.length());
    }
}

void trim_prefix(std::string& s, const char prefix) {
    if (has_prefix(s, prefix)) {
        s.erase(0, 1);
    }
}

bool has_suffix(const std::string& s, const std::string& suffix) {
    return s.compare(s.length() - suffix.length(), suffix.length(), suffix) ==
           0;
}

bool has_suffix(const std::string& s, const char prefix) {
    return s.length() > 0 && s.at(s.length() - 1) == prefix;
}

void trim_suffix(std::string& s, const char suffix) {
    if (has_suffix(s, suffix)) {
        s.pop_back();
    }
}

void trim_suffix(std::string& s, const std::string& suffix) {
    if (has_suffix(s, suffix)) {
        s.erase(s.length() - suffix.length());
    }
}

void trim_right(std::string& s, const std::string& cutset) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [&cutset](int ch) {
                             return cutset.find(ch) == std::string::npos;
                         })
                .base(),
            s.end());
}

void trim_right(std::string& s, const char c) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [c](int ch) { return c != ch; })
                .base(),
            s.end());
}

void trim_left(std::string& s, const std::string& cutset) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&cutset](int ch) {
                return cutset.find(ch) == std::string::npos;
            }));
}

void trim_left(std::string& s, const char c) {
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [c](int ch) { return c != ch; }));
}

std::optional<std::string> find_line(const std::string& s,
                                     const std::string& prefix) {
    size_t i = 0, j = 0;
    while ((i = s.find('\n', j)) != std::string::npos) {
        auto line = s.substr(j, i - j);
        if (has_prefix(line, prefix)) {
            return line;
        }
        j = i + 1;
    }
    return {};
}

void mark_int(std::string& n) { n += "i"; }

using VCFunc =
    std::function<void(std::string&, const std::string&, char* buf, size_t)>;

struct Cmd {
    Cmd(const std::string name, const std::string arg, VCFunc func)
        : name(name), arg(arg), func(func) {}
    Cmd(const std::string name, VCFunc func) : name(name), func(func) {}
    std::string name;
    std::optional<std::string> arg;
    VCFunc func;
};

std::string hostname() {
    auto buf = (char*)calloc(HOST_NAME_MAX, 1);
    if (gethostname(buf, HOST_NAME_MAX - 1)) {
        return "???";
    }
    return std::string(buf);
}

void measure_temp(std::string& dst, const std::string&, char* buf, size_t len) {
    if (vc_gencmd(buf, len, "measure_temp")) {
        throw std::runtime_error("unable to measure CPU temperature");
    }
    int n;
    char* val;
    if (vc_gencmd_string_property(buf, "temp", &val, &n) == 0) {
        throw std::runtime_error("unable to parse vc_gencmd output");
    }
    auto&& t = std::string(val, (size_t)n);
    trim_suffix(t, "'C");
    dst += t;
}

void measure_clock(std::string& dst, const std::string& arg, char* buf,
                   size_t len) {
    if (vc_gencmd(buf, len, "%s %s", "measure_clock", arg.c_str())) {
        throw std::runtime_error("unable to measure " + arg + " frequency");
    }

    // TODO(eric): vc_gencmd_string_property does not work with
    // frequency(dd)=ddddd.

    auto&& t = std::string(buf);
    const auto i = t.find('=');
    if (i != std::string::npos) {
        t.erase(0, i + 1);
    }
    mark_int(t);
    dst += t;
}

void measure_volts(std::string& dst, const std::string& arg, char* buf,
                   size_t len) {
    if (vc_gencmd(buf, len, "%s %s", "measure_volts", arg.c_str())) {
        throw std::runtime_error("unable to measure " + arg + " voltage");
    }
    int n;
    char* val;
    if (vc_gencmd_string_property(buf, "volt", &val, &n) == 0) {
        throw std::runtime_error("unable to parse vc_gencmd output");
    }
    auto&& t = std::string(val, (size_t)n);
    trim_suffix(t, 'V');
    trim_right(t, '0');
    dst += t;
}

/*
    var underVoltage            = (number >> 0)  & 1 ? true : false;
    var frequencyCapped         = (number >> 1)  & 1 ? true : false;
    var throttled               = (number >> 2)  & 1 ? true : false;
    var softTempLimit           = (number >> 3)  & 1 ? true : false;
    var underVoltageOccurred    = (number >> 16) & 1 ? true : false;
    var frequencyCappedOccurred = (number >> 17) & 1 ? true : false;
    var throttledOccurred       = (number >> 18) & 1 ? true : false;
    var softTempLimitOccurred   = (number >> 19) & 1 ? true : false;
*/
std::map<std::string,int> getThrottledFlags(unsigned int value) {

    std::map<std::string,int> flags;

    flags[std::string("under_voltage")]     = (value >> 0) & 1;
    flags[std::string("frequency_cap")]     = (value >> 1) & 1;
    flags[std::string("throttled")]         = (value >> 2) & 1;
    flags[std::string("soft_temp_limit")]   = (value >> 3) & 1;
    flags[std::string("under_voltage_occurred")]    = (value >> 16) & 1;
    flags[std::string("frequency_cap_occurred")]    = (value >> 17) & 1;
    flags[std::string("throttled_occurred")]        = (value >> 18) & 1;
    flags[std::string("soft_temp_limit_occurred")]  = (value >> 19) & 1;

    return flags;
}

void get_throttled(std::string& dst, const std::string& arg, char* buf,
                   size_t len) {
    if (vc_gencmd(buf, len, "get_throttled")) {
        throw std::runtime_error("unable to get CPU throttling information");
    }
    int n;
    char* val;
    if (vc_gencmd_string_property(buf, "throttled", &val, &n) == 0) {
        throw std::runtime_error("unable to parse vc_gencmd output");
    }
    unsigned int flags = std::stoul(val, nullptr, 16);
    std::map<std::string,int> map = getThrottledFlags(flags);

    auto search = map.find(arg);
    dst += std::to_string(search->second);
}

void get_config(std::string& dst, const std::string& arg, char* buf,
                size_t len) {
    if (vc_gencmd(buf, len, "%s %s", "get_config", arg.c_str())) {
        throw std::runtime_error("unable to measure " + arg + " voltage");
    }
    int n;
    char* val;
    if (vc_gencmd_string_property(buf, arg.c_str(), &val, &n) == 0) {
        throw std::runtime_error("unable to parse vc_gencmd output");
    }
    auto&& t = std::string(val, n);
    if (t != "0") {
        t += "000000";
    }
    mark_int(t);
    dst += t;
}

void get_mem(std::string& dst, const std::string& arg, char* buf, size_t len) {
    if (vc_gencmd(buf, len, "%s %s", "get_mem", arg.c_str())) {
        throw std::runtime_error("unable to measure " + arg + " voltage");
    }
    int n;
    char* val;
    if (vc_gencmd_string_property(buf, arg.c_str(), &val, &n) == 0) {
        throw std::runtime_error("unable to parse vc_gencmd output");
    }
    auto&& t = std::string(val, (size_t)n);
    trim_suffix(t, 'M');
    if (t != "0") {
        t += "000000i";
    } else {
        t += 'i';
    }
    dst += t;
}

void mem_oom_count(std::string& dst, const std::string&, char* buf,
                   size_t len) {
    if (vc_gencmd(buf, len, "mem_oom")) {
        throw std::runtime_error("unable to measure OOM errors");
    }
    static const std::string prefix = "oom events";
    auto&& line = find_line(std::string(buf), prefix);
    if (!line.has_value()) {
        throw std::runtime_error("unable to find '" + prefix + "' line");
    }
    auto&& t = line.value();
    trim_prefix(t, prefix);
    trim_prefix(t, ':');
    trim_left(t, ' ');
    mark_int(t);
    dst += t;
}

void mem_oom_ms(std::string& dst, const std::string&, char* buf, size_t len) {
    if (vc_gencmd(buf, len, "mem_oom")) {
        throw std::runtime_error("unable to measure time spent in OOM handler");
    }
    static const std::string prefix = "total time in oom handler";
    auto&& line = find_line(std::string(buf), prefix);
    if (!line.has_value()) {
        throw std::runtime_error("unable to find '" + prefix + "' line");
    }
    auto&& t = line.value();
    trim_prefix(t, prefix);
    trim_prefix(t, ':');
    trim_left(t, ' ');
    trim_suffix(t, " ms");
    mark_int(t);
    dst += t;
}

VCFunc mem_reloc_stats(const std::string& prefix) {
    auto fn = [=](std::string& dst, const std::string&, char* buf,
                  size_t len) -> void {
        if (vc_gencmd(buf, len, "mem_reloc_stats")) {
            throw std::runtime_error("unable to measure memory reloc stats");
        }
        auto&& line = find_line(std::string(buf), prefix);
        if (!line.has_value()) {
            throw std::runtime_error("unable to find '" + prefix + "' line");
        }
        auto&& t = line.value();
        trim_prefix(t, prefix);
        trim_prefix(t, ':');
        trim_left(t, ' ');
        mark_int(t);
        dst += t;
    };
    return fn;
}

const std::map<std::string, Cmd> cmds = {
    {"soc_temp", Cmd("measure_temp", measure_temp)},
    {"arm_freq", Cmd("measure_clock", "arm", measure_clock)},
    {"core_freq", Cmd("measure_clock", "core", measure_clock)},

    {"h264_freq", Cmd("measure_clock", "h264", measure_clock)},
    {"isp_freq", Cmd("measure_clock", "isp", measure_clock)},
    {"v3d_freq", Cmd("measure_clock", "v3d", measure_clock)},
    {"uart_freq", Cmd("measure_clock", "uart", measure_clock)},
    {"pwm_freq", Cmd("measure_clock", "pwm", measure_clock)},
    {"emmc_freq", Cmd("measure_clock", "emmc", measure_clock)},
    {"pixel_freq", Cmd("measure_clock", "pixel", measure_clock)},
    {"vec_freq", Cmd("measure_clock", "vec", measure_clock)},
    {"hdmi_freq", Cmd("measure_clock", "hdmi", measure_clock)},
    {"dpi_freq", Cmd("measure_clock", "dpi", measure_clock)},

    {"core_volts", Cmd("measure_volts", "core", measure_volts)},
    {"sdram_c_volts", Cmd("measure_volts", "sdram_c", measure_volts)},
    {"sdram_i_volts", Cmd("measure_volts", "sdram_i", measure_volts)},
    {"sdram_p_volts", Cmd("measure_volts", "sdram_p", measure_volts)},

    {"config_arm_freq", Cmd("get_config", "arm_freq", get_config)},
    {"config_core_freq", Cmd("get_config", "core_freq", get_config)},
    {"config_gpu_freq", Cmd("get_config", "gpu_freq", get_config)},
    {"config_sdram_freq", Cmd("get_config", "sdram_freq", get_config)},

    {"arm_mem", Cmd("get_mem", "arm", get_mem)},
    {"gpu_mem", Cmd("get_mem", "gpu", get_mem)},
    {"malloc_total_mem", Cmd("get_mem", "malloc_total", get_mem)},
    {"malloc_mem", Cmd("get_mem", "malloc", get_mem)},
    {"reloc_total_mem", Cmd("get_mem", "reloc_total", get_mem)},
    {"reloc_mem", Cmd("get_mem", "reloc", get_mem)},

    {"oom_count", Cmd("mem_oom", mem_oom_count)},
    {"oom_ms", Cmd("mem_oom", mem_oom_ms)},

    {"mem_reloc_allocation_failures",
     Cmd("mem_reloc_stats", mem_reloc_stats("alloc failures"))},
    {"mem_reloc_compactions",
     Cmd("mem_reloc_stats", mem_reloc_stats("compactions"))},
    {"mem_reloc_legacy_block_failures",
     Cmd("mem_reloc_stats", mem_reloc_stats("legacy block fails"))},

    {"under_voltage_occurred", Cmd("get_throttled", "under_voltage_occurred", get_throttled)},
    {"frequency_cap_occurred", Cmd("get_throttled", "frequency_cap_occurred", get_throttled)},
    {"throttled_occurred", Cmd("get_throttled", "throttled_occurred", get_throttled)},
    {"soft_temp_limit_occurred", Cmd("get_throttled", "soft_temp_limit_occurred", get_throttled)},
    {"under_voltage", Cmd("get_throttled", "under_voltage", get_throttled)},
    {"frequency_cap", Cmd("get_throttled", "frequency_cap", get_throttled)},
    {"throttled", Cmd("get_throttled", "throttled", get_throttled)},
    {"soft_temp_limit", Cmd("get_throttled", "soft_temp_limit", get_throttled)},
 
};

const struct option long_opts[] = {{"step", required_argument, NULL, 's'},
                                   {"help", no_argument, NULL, 'h'},
                                   {NULL, 0, NULL, 0}};

int main(int argc, char* argv[]) {
    auto step = 1s;

    int ch;
    while ((ch = getopt_long(argc, argv, "s:h", long_opts, NULL)) != -1) {
        switch (ch) {
        case 's':
            step = std::chrono::seconds(std::stoi(optarg));
            break;
        case 'h':
            fatal("Usage: " + std::string(argv[0]) + " [-step seconds]");
            break;
        default:
            return 1;
        }
    }

    vcos_init();

    VCHI_INSTANCE_T vchi_instance;
    if (vchi_initialise(&vchi_instance) != 0) {
        fatal("unable to initialize VCHI instance");
    }

    if (vchi_connect(NULL, 0, vchi_instance) != 0) {
        fatal("unable to create VCHI connection");
    }

    VCHI_CONNECTION_T* vchi_connection = NULL;
    vc_vchi_gencmd_init(vchi_instance, &vchi_connection, 1);

    auto host = hostname();

    constexpr size_t N = 4096;
    char tmp[N];
    std::string buf;
    while (true) {
        // Measurement and tag set.
        buf += "raspberry_pi,host=";
        buf += host;
        buf += ' ';

        // Field set.
        for (const auto& e : cmds) {
            buf += e.first;
            buf += '=';

            auto&& val = e.second;
            try {
                val.func(buf, val.arg.value_or(""), tmp, N);
            } catch (const std::exception& e) {
                fatal(e.what());
            }
            buf += ',';
        }
        trim_suffix(buf, ',');

        // Timestamp.
        buf += ' ';
        buf += std::to_string(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch())
                .count());

        std::cout << buf << std::endl;
        buf.clear();

        std::this_thread::sleep_for(std::chrono::seconds(step));
    }

    vc_gencmd_stop();

    if (vchi_disconnect(vchi_instance) != 0) {
        fatal("VCHI disconnect failed");
    }
}

