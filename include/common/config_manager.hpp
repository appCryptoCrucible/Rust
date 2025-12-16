#pragma once
#include <string>
#include <unordered_map>
#include <optional>

class ConfigManager {
public:
  static void Initialize(const std::string& env_path = ".env");
  static std::optional<std::string> Get(const std::string& key);
  static std::string GetOrThrow(const std::string& key);
  static int GetIntOr(const std::string& key, int default_value);
  static double GetDoubleOr(const std::string& key, double default_value);
  static bool GetBoolOr(const std::string& key, bool default_value);
private:
  static std::unordered_map<std::string, std::string> cache_;
  static void LoadEnvFile(const std::string& env_path);
};

