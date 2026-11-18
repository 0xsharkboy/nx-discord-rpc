#pragma once

// A Transport serving canned HTTP responses by host and request target, for the updater tests.

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "drpc/transport.hpp"

namespace test {

class FakeHttps : public drpc::Transport {
public:
    // Responses are raw HTTP; key is "host" + "target" (e.g. "api.github.com/repos/x").
    std::map<std::string, std::string> responses;
    std::vector<std::string> requests;  // "host target", in order
    std::vector<std::string> heads;     // full request heads
    size_t read_size = 7;               // small reads exercise the incremental parser
    bool fail_connect = false;

    bool connect(std::string_view host, uint16_t) override {
        if (fail_connect) return false;
        host_ = std::string(host);
        req_.clear();
        out_.clear();
        pos_ = 0;
        open_ = true;
        return true;
    }

    int read(std::span<uint8_t> buf, int) override {
        if (!open_) return -1;
        if (out_.empty() && pos_ == 0) answer();
        if (pos_ >= out_.size()) return -1;
        const size_t n = std::min({buf.size(), read_size, out_.size() - pos_});
        std::copy_n(out_.data() + pos_, n, buf.data());
        pos_ += n;
        return int(n);
    }

    bool write_all(std::span<const uint8_t> data) override {
        req_.append(reinterpret_cast<const char*>(data.data()), data.size());
        return open_;
    }

    void close() override { open_ = false; }

private:
    void answer() {
        heads.push_back(req_);
        const size_t sp1 = req_.find(' ');
        const size_t sp2 = req_.find(' ', sp1 + 1);
        const std::string target = req_.substr(sp1 + 1, sp2 - sp1 - 1);
        requests.push_back(host_ + " " + target);
        const auto it = responses.find(host_ + target);
        out_ = it == responses.end() ? "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n" : it->second;
    }

    std::string host_, req_, out_;
    size_t pos_ = 0;
    bool open_ = false;
};

inline std::string ok(const std::string& body) {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

inline std::string redirect(const std::string& location, int status = 302) {
    return "HTTP/1.1 " + std::to_string(status) + " Found\r\nLocation: " + location + "\r\nContent-Length: 0\r\n\r\n";
}

}  // namespace test
