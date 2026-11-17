#include <doctest/doctest.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {

// Minimal walker for the i18n files: nested objects whose leaves are strings.
// Maps each dotted leaf path to its value.
struct Walker {
    const std::string& s;
    size_t i = 0;
    bool ok = true;
    std::map<std::string, std::string> leaves;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    std::string str() {
        std::string out;
        if (i >= s.size() || s[i] != '"') { ok = false; return out; }
        for (++i; i < s.size() && s[i] != '"'; ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) ++i;
            out += s[i];
        }
        if (i >= s.size()) ok = false; else ++i;
        return out;
    }
    void object(const std::string& prefix) {
        ws();
        if (i >= s.size() || s[i] != '{') { ok = false; return; }
        ++i;
        for (;;) {
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return; }
            const std::string key = str();
            ws();
            if (!ok || i >= s.size() || s[i] != ':') { ok = false; return; }
            ++i;
            ws();
            const std::string path = prefix.empty() ? key : prefix + "." + key;
            if (i < s.size() && s[i] == '{') {
                object(path);
            } else {
                const std::string v = str();
                if (!leaves.emplace(path, v).second) ok = false;  // duplicate key
            }
            if (!ok) return;
            ws();
            if (i < s.size() && s[i] == ',') ++i;
        }
    }
};

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

size_t count_placeholders(const std::string& v) {
    size_t n = 0;
    for (size_t p = v.find("{}"); p != std::string::npos; p = v.find("{}", p + 2)) ++n;
    return n;
}

}  // namespace

TEST_CASE("i18n_keys_match: en-US and fr have the same keys and placeholders") {
    const std::string dir = NXDRPC_I18N_DIR;
    const std::string en_src = read_file(dir + "/en-US/app.json");
    const std::string fr_src = read_file(dir + "/fr/app.json");
    REQUIRE_FALSE(en_src.empty());
    REQUIRE_FALSE(fr_src.empty());
    CHECK(fr_src.find("\xE2\x80\x94") == std::string::npos);  // no em dash

    Walker en{en_src}, fr{fr_src};
    en.object("");
    fr.object("");
    REQUIRE(en.ok);
    REQUIRE(fr.ok);
    REQUIRE_FALSE(en.leaves.empty());

    for (const auto& [k, v] : en.leaves) {
        const auto it = fr.leaves.find(k);
        REQUIRE_MESSAGE(it != fr.leaves.end(), "missing in fr: ", k);
        CHECK_MESSAGE(count_placeholders(v) == count_placeholders(it->second), "placeholders differ: ", k);
    }
    for (const auto& [k, v] : fr.leaves) CHECK_MESSAGE(en.leaves.count(k) == 1, "extra in fr: ", k);
}
