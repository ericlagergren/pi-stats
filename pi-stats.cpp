#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

extern "C" {
#include <unistd.h>

#include <bcm_host.h>
#include <interface/vmcs_host/vc_vchi_gencmd.h>
}

void fatal(const std::string& msg) {
    std::cerr << msg << std::endl;
    std::exit(1);
}

void trim_prefix(std::string& s, const std::string& prefix) {
    if (s.compare(0, prefix.length(), prefix) == 0) {
        s.erase(0, prefix.length());
    }
}

void trim_suffix(std::string& s, const std::string& suffix) {
    if (s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0) {
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

void mul_1e6(std::string& n) {
    if (n != "0") {
        n += "000000";
    }
}

void make_int(std::string& n) { n += "i"; }

using VcFunc =
    std::function<void(std::string&, const std::string&, char*, size_t)>;

struct Cmd {
    Cmd(const std::string name, const std::optional<const std::string> arg,
        VcFunc func)
        : name(name), arg(arg), func(func) {}
    Cmd(const std::string name, VcFunc func) : name(name), func(func) {}
    std::string name;
    std::optional<std::string> arg;
    VcFunc func;
};

void hostname(std::string& dst, const std::string&, char* buf, size_t len) {
    auto buf_ = buf;
    if (len < HOST_NAME_MAX) {
        buf_ = (char*)calloc(HOST_NAME_MAX, 1);
    } else {
        buf_[len - 1] = '\0';
    }
    if (gethostname(buf_, HOST_NAME_MAX - 1)) {
        dst += "???";
    } else {
        dst += std::string(buf_);
    }
    if (buf != buf_) {
        free(buf_);
    }
}

void measure_temp(std::string& dst, const std::string&, char* buf,
                  [[maybe_unused]] size_t len) {
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
    trim_right(t, "0");
    trim_suffix(t, ".");
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
    const auto i = t.find("=");
    if (i != std::string::npos) {
        t.erase(0, i + 1);
    }
    mul_1e6(t);
    make_int(t);
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
    trim_suffix(t, "V");
    trim_right(t, "0");
    dst += t;
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
    mul_1e6(t);
    make_int(t);
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
    trim_suffix(t, "M");
    if (t != "0") {
        t += "000000i";
    } else {
        t += "i";
    }
    dst += t;
}

const std::unordered_map<std::string, Cmd> cmds = {
    {"host", Cmd("hostname", hostname)},

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

    // TODO(eric): implement these
    /* {"oom_c", Cmd("mem_oom", ...)}, */
    /* {"oom_t", Cmd("mem_oom", ...)}, */
    /* */
    /* {"mem_reloc_alloc_fail_c", Cmd("mem_reloc_stats", ...)}, */
    /* {"mem_reloc_compact_c", Cmd("mem_reloc_stats", ...)}, */
    /* {"mem_reloc_leg_blk_fail_C", Cmd("mem_reloc_stats", ...)}, */
};

int main(void) {
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

    std::string buf;
    while (true) {
        buf += "raspberry_pi";

        constexpr size_t N = 4096;
        char tmp[N];
        for (const auto& [key, val] : cmds) {
            buf += ",";
            buf += key;
            buf += "=";

            try {
                val.func(buf, val.arg.value_or(""), tmp, N);
            } catch (const std::exception& e) {
                fatal(e.what());
            }
        }
        std::cout << buf << std::endl;
        buf.clear();

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    vc_gencmd_stop();

    if (vchi_disconnect(vchi_instance) != 0) {
        fatal("VCHI disconnect failed");
    }
}
