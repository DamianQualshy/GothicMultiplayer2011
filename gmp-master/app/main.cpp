/*
MIT License

Copyright (c) 2025 Gothic Multiplayer Team.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <httplib.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Endpoint {
  std::string address;
  std::uint16_t port{};
};

struct ServerRecord {
  Endpoint endpoint;
  nlohmann::json payload;
  std::string auth_key;
  std::chrono::steady_clock::time_point last_update;
};

struct Options {
  std::string host{"0.0.0.0"};
  int port{8080};
  std::string path{"/servers"};
  std::chrono::seconds ttl{std::chrono::seconds{60}};
};

class ServerRegistry {
public:
  explicit ServerRegistry(std::chrono::seconds ttl) : ttl_{ttl} {
  }

  bool Update(const Endpoint& endpoint, nlohmann::json payload, std::string auth_key) {
    std::lock_guard lock{mutex_};
    RemoveExpiredLocked();

    auto key = MakeKey(endpoint);
    auto& entry = servers_[key];

    if (!entry.auth_key.empty() && !auth_key.empty() && entry.auth_key != auth_key) {
      return false;
    }

    if (!entry.auth_key.empty() && auth_key.empty()) {
      return false;
    }

    entry.endpoint = endpoint;
    entry.payload = std::move(payload);
    if (!auth_key.empty()) {
      entry.auth_key = std::move(auth_key);
    }
    entry.last_update = std::chrono::steady_clock::now();
    return true;
  }

  std::vector<ServerRecord> Snapshot() {
    std::lock_guard lock{mutex_};
    RemoveExpiredLocked();

    std::vector<ServerRecord> result;
    result.reserve(servers_.size());
    for (const auto& [_, record] : servers_) {
      result.push_back(record);
    }
    return result;
  }

private:
  static std::string MakeKey(const Endpoint& endpoint) {
    return endpoint.address + ":" + std::to_string(endpoint.port);
  }

  void RemoveExpiredLocked() {
    if (ttl_ <= std::chrono::seconds::zero()) {
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    for (auto it = servers_.begin(); it != servers_.end();) {
      if (now - it->second.last_update > ttl_) {
        SPDLOG_INFO("Removing stale server {}:{}", it->second.endpoint.address, it->second.endpoint.port);
        it = servers_.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::mutex mutex_;
  std::unordered_map<std::string, ServerRecord> servers_;
  std::chrono::seconds ttl_;
};

void Trim(std::string& text) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  auto begin = std::find_if_not(text.begin(), text.end(), is_space);
  auto end = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
  if (begin >= end) {
    text.clear();
  } else {
    text.assign(begin, end);
  }
}

std::string SanitizeText(std::string text) {
  std::string result;
  result.reserve(text.size());
  for (unsigned char ch : text) {
    if (ch >= 0x20 || ch == 0x07) {
      result.push_back(static_cast<char>(ch));
    }
  }
  return result;
}

std::optional<Endpoint> ParseEndpoint(std::string key, const std::string& remote_address) {
  Trim(key);
  if (key.empty()) {
    return std::nullopt;
  }

  auto colon_pos = key.find_last_of(':');
  if (colon_pos == std::string::npos) {
    return std::nullopt;
  }

  std::string host = key.substr(0, colon_pos);
  std::string port_str = key.substr(colon_pos + 1);
  Trim(host);
  Trim(port_str);

  if (host.empty()) {
    host = remote_address;
  }

  if (host == "0.0.0.0" || host == "::") {
    host = remote_address;
  }

  if (host.empty()) {
    return std::nullopt;
  }

  int port = 0;
  auto result = std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
  if (result.ec != std::errc{} || port <= 0 || port > 65535) {
    return std::nullopt;
  }

  Endpoint endpoint;
  endpoint.address = std::move(host);
  endpoint.port = static_cast<std::uint16_t>(port);
  return endpoint;
}

nlohmann::json SanitizePayload(const nlohmann::json& payload, std::string& auth_key) {
  nlohmann::json sanitized = nlohmann::json::object();

  if (!payload.is_object()) {
    return sanitized;
  }

  for (auto it = payload.begin(); it != payload.end(); ++it) {
    const auto& key = it.key();
    if (key == "auth_key") {
      if (it->is_string()) {
        auth_key = SanitizeText(it->get<std::string>());
      }
      continue;
    }

    if (it->is_string()) {
      sanitized[key] = SanitizeText(it->get<std::string>());
    } else if (it->is_number_integer()) {
      sanitized[key] = it->get<std::int64_t>();
    } else if (it->is_number_unsigned()) {
      sanitized[key] = it->get<std::uint64_t>();
    } else if (it->is_number_float()) {
      sanitized[key] = it->get<double>();
    } else if (it->is_boolean()) {
      sanitized[key] = it->get<bool>();
    }
  }

  return sanitized;
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string_view argument{argv[i]};
    if (argument.rfind("--host=", 0) == 0) {
      options.host = std::string(argument.substr(7));
    } else if (argument.rfind("--port=", 0) == 0) {
      int port = options.port;
      auto value = argument.substr(7);
      auto result = std::from_chars(value.data(), value.data() + value.size(), port);
      if (result.ec == std::errc{} && port > 0 && port < 65536) {
        options.port = port;
      }
    } else if (argument.rfind("--path=", 0) == 0) {
      options.path = std::string(argument.substr(7));
    } else if (argument.rfind("--ttl=", 0) == 0) {
      int ttl_value = static_cast<int>(options.ttl.count());
      auto value = argument.substr(6);
      auto result = std::from_chars(value.data(), value.data() + value.size(), ttl_value);
      if (result.ec == std::errc{} && ttl_value >= 0) {
        options.ttl = std::chrono::seconds{ttl_value};
      }
    }
  }

  if (options.path.empty() || options.path.front() != '/') {
    options.path.insert(options.path.begin(), '/');
  }

  return options;
}

nlohmann::json BuildResponsePayload(const std::vector<ServerRecord>& records) {
  nlohmann::json servers = nlohmann::json::array();

  for (const auto& record : records) {
    nlohmann::json server = record.payload;
    if (!server.is_object()) {
      server = nlohmann::json::object();
    }

    server["ip"] = record.endpoint.address;
    server["address"] = record.endpoint.address;
    server["port"] = record.endpoint.port;

    servers.push_back(std::move(server));
  }

  nlohmann::json response = nlohmann::json::object();
  response["servers"] = std::move(servers);
  return response;
}

void ConfigureServerRoutes(httplib::Server& server, ServerRegistry& registry, const Options& options) {
  server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
    nlohmann::json body{{"error", "Resource not found"}};
    res.status = 404;
    res.set_content(body.dump(), "application/json");
  });

  server.Get(options.path, [&registry](const httplib::Request&, httplib::Response& res) {
    auto records = registry.Snapshot();
    auto payload = BuildResponsePayload(records);
    res.set_content(payload.dump(), "application/json");
  });

  server.Post(options.path, [&registry](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body);
    } catch (const std::exception& ex) {
      SPDLOG_WARN("Failed to parse master server update from {}: {}", req.remote_addr, ex.what());
      res.status = 400;
      nlohmann::json error{{"error", "Invalid JSON payload"}};
      res.set_content(error.dump(), "application/json");
      return;
    }

    if (!body.is_object()) {
      SPDLOG_WARN("Ignoring master server update from {}: payload is not an object", req.remote_addr);
      res.status = 400;
      nlohmann::json error{{"error", "Payload must be a JSON object"}};
      res.set_content(error.dump(), "application/json");
      return;
    }

    bool updated = false;
    for (auto it = body.begin(); it != body.end(); ++it) {
      auto endpoint_opt = ParseEndpoint(it.key(), req.remote_addr);
      if (!endpoint_opt) {
        SPDLOG_WARN("Ignoring entry '{}' from {}: invalid endpoint", it.key(), req.remote_addr);
        continue;
      }

      std::string auth_key;
      auto sanitized_payload = SanitizePayload(*it, auth_key);
      if (!registry.Update(*endpoint_opt, std::move(sanitized_payload), std::move(auth_key))) {
        SPDLOG_WARN("Rejected update for {}:{} from {} due to failed authentication", endpoint_opt->address, endpoint_opt->port, req.remote_addr);
        continue;
      }

      SPDLOG_INFO("Updated server {}:{} from {}", endpoint_opt->address, endpoint_opt->port, req.remote_addr);
      updated = true;
    }

    if (!updated) {
      res.status = 400;
      nlohmann::json error{{"error", "No valid server entries were provided"}};
      res.set_content(error.dump(), "application/json");
      return;
    }

    res.status = 204;
  });
}

}  // namespace

int main(int argc, char** argv) {
  auto options = ParseOptions(argc, argv);
  SPDLOG_INFO("Starting master server on {}:{}{}", options.host, options.port, options.path);

  httplib::Server server;
  ServerRegistry registry{options.ttl};

  ConfigureServerRoutes(server, registry, options);

  if (!server.listen(options.host.c_str(), options.port)) {
    SPDLOG_ERROR("Failed to start master server on {}:{}", options.host, options.port);
    return 1;
  }

  return 0;
}