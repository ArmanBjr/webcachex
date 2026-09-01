#include "logger.h"

#include <fstream>
#include <sstream>

// Escape CSV field:
// - wrap in quotes if contains comma, quote, or newline
// - double quotes inside
std::string CsvLogger::csv_escape(const std::string& s) {
  bool need_quotes = false;
  for (char c : s) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      need_quotes = true;
      break;
    }
  }
  if (!need_quotes) return s;

  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (char c : s) {
    if (c == '"') out.push_back('"'); // double it
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

CsvLogger::CsvLogger(std::string filepath) : filepath_(std::move(filepath)) {}

void CsvLogger::ensure_header_exists_unlocked() {
  // If file doesn't exist or is empty, write header
  std::ifstream in(filepath_, std::ios::binary);
  bool need_header = true;
  if (in.good()) {
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    need_header = (size <= 0);
  }
  in.close();

  if (need_header) {
    std::ofstream out(filepath_, std::ios::app);
    out << "timestamp,host,path,result,origin_selected,response_time_ms\n";
  }
}

void CsvLogger::log(std::string timestamp_iso,
                    std::string host,
                    std::string path,
                    std::string result,
                    std::string origin_selected,
                    long long response_time_ms) {
  std::lock_guard<std::mutex> lk(mu_);

  ensure_header_exists_unlocked();

  std::ofstream out(filepath_, std::ios::app);
  out
    << csv_escape(timestamp_iso) << ","
    << csv_escape(host) << ","
    << csv_escape(path) << ","
    << csv_escape(result) << ","
    << csv_escape(origin_selected) << ","
    << response_time_ms
    << "\n";
}
