#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
namespace arli {
struct JobLogBusy:std::runtime_error { using std::runtime_error::runtime_error; };
struct JobLogPosition {
  std::string generation;
  std::uint64_t offset=0;
  std::string encode() const;
  static JobLogPosition parse(const std::string& encoded);
};
struct JobLogSlice {
  std::string text;
  JobLogPosition start,next;
  bool gap=false,end=false;
};
// Each snapshot opens only the two retained generations; no unbounded stale FDs.
JobLogSlice read_job_log(const std::string& path,const JobLogPosition& after);
}
