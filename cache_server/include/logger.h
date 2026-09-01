#pragma once

#include <mutex>
#include <string>

class CsvLogger {
public:
  explicit CsvLogger(std::string filepath);

  // One log line per request
  void log(std::string timestamp_iso,
           std::string host,
           std::string path,
           std::string result,          // HIT/MISS/EXPIRE/ERROR
           std::string origin_selected, // "127.0.0.1:8081"
           long long response_time_ms);

private:
  std::string filepath_;
  std::mutex mu_;

  static std::string csv_escape(const std::string& s);
  void ensure_header_exists_unlocked();
};
