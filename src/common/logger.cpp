#include "common/logger.hpp"
#include <iomanip>

std::unique_ptr<Logger> Logger::instance_;
std::mutex Logger::instance_mutex_;

static std::string NowToString(const std::chrono::system_clock::time_point& tp) {
  std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm_buf;
#if defined(_WIN32)
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
  return std::string(buf);
}

void Logger::Initialize(const std::string& path, LogLevel min_level) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) instance_.reset(new Logger());
  instance_->min_level_ = min_level;
  instance_->log_file_.open(path, std::ios::out | std::ios::app);
  instance_->running_ = true;
  instance_->worker_thread_ = std::thread(&Logger::WorkerFunction, instance_.get());
}

void Logger::Shutdown() {
  std::unique_ptr<Logger> inst;
  {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    inst = std::move(instance_);
  }
  if (!inst) return;
  {
    std::lock_guard<std::mutex> lock(inst->log_mutex_);
    inst->running_ = false;
  }
  inst->cv_.notify_all();
  if (inst->worker_thread_.joinable()) inst->worker_thread_.join();
  if (inst->log_file_.is_open()) inst->log_file_.close();
}

Logger::~Logger() { Shutdown(); }

void Logger::WorkerFunction() {
  while (true) {
    std::unique_lock<std::mutex> lock(log_mutex_);
    cv_.wait(lock, [&]{ return !log_queue_.empty() || !running_; });
    if (!running_ && log_queue_.empty()) break;
    auto entry = log_queue_.front();
    log_queue_.pop();
    lock.unlock();
    WriteLogEntry(entry);
  }
}

std::string Logger::LevelToString(LogLevel l) {
  switch (l) {
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO: return "INFO";
    case LogLevel::WARNING: return "WARN";
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::CRITICAL: return "CRIT";
  }
  return "UNK";
}

std::string Logger::FormatLogEntry(const LogEntry& e) {
  std::ostringstream oss;
  oss << NowToString(e.timestamp) << " [" << LevelToString(e.level) << "]"
      << " (" << e.thread_id << ") " << e.file << ":" << e.line << " - "
      << e.message << '\n';
  return oss.str();
}

void Logger::WriteLogEntry(const LogEntry& e) {
  std::string line = FormatLogEntry(e);
  if (log_file_.is_open()) {
    log_file_ << line;
    log_file_.flush();
  }
}

void Logger::Log(LogLevel level, const std::string& message, const std::string& file, int line) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) return;
  if (level < instance_->min_level_) return;
  LogEntry e{std::chrono::system_clock::now(), level, message, file, line, std::this_thread::get_id()};
  {
    std::lock_guard<std::mutex> qlock(instance_->log_mutex_);
    instance_->log_queue_.push(e);
  }
  instance_->cv_.notify_one();
}

void Logger::Debug(const std::string& m, const std::string& f, int l) { Log(LogLevel::DEBUG, m, f, l); }
void Logger::Info(const std::string& m, const std::string& f, int l) { Log(LogLevel::INFO, m, f, l); }
void Logger::Warning(const std::string& m, const std::string& f, int l) { Log(LogLevel::WARNING, m, f, l); }
void Logger::Error(const std::string& m, const std::string& f, int l) { Log(LogLevel::ERROR, m, f, l); }
void Logger::Critical(const std::string& m, const std::string& f, int l) { Log(LogLevel::CRITICAL, m, f, l); }

