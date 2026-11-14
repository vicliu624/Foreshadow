#pragma once
#include "signalscope/backend/distribution/distribution.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "test_process.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <archive.h>
#include <archive_entry.h>
#include <fstream>
#include <openssl/evp.h>
using namespace signalscope::distribution;
using amc_test::require;
namespace {
template <class F> void rejected(F &&fn, const std::string &code = {}) {
    bool failed = false;
    try {
        fn();
    } catch (const Error &error) {
        failed = true;
        if (!code.empty())
            require(error.code == code, ("Expected structured failure " + code + "; got " + error.code).c_str());
    }
    require(failed, "Unsafe or incompatible operation was accepted");
}
std::string hex(const unsigned char *p, std::size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[p[i] >> 4];
        out += digits[p[i] & 15];
    }
    return out;
}
struct Signing {
    EVP_PKEY *key = nullptr;
    std::string publicKey;
    Signing() {
        auto *context = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        require(context && EVP_PKEY_keygen_init(context) == 1 && EVP_PKEY_keygen(context, &key) == 1,
                "Could not create fixture key");
        EVP_PKEY_CTX_free(context);
        unsigned char bytes[32];
        std::size_t size = 32;
        require(EVP_PKEY_get_raw_public_key(key, bytes, &size) == 1, "Could not export fixture public key");
        publicKey = hex(bytes, size);
    }
    ~Signing() { EVP_PKEY_free(key); }
    std::string sign(const Json &data) const {
        const auto payload = data.dump();
        auto *context = EVP_MD_CTX_new();
        unsigned char signature[64];
        std::size_t size = 64;
        require(EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) == 1 &&
                    EVP_DigestSign(context, signature, &size, reinterpret_cast<const unsigned char *>(payload.data()),
                                   payload.size()) == 1,
                "Could not sign fixture");
        EVP_MD_CTX_free(context);
        unsigned char bytes[32];
        std::size_t length = 32;
        EVP_PKEY_get_raw_public_key(key, bytes, &length);
        return Json{{"schema", 1},
                    {"payload", payload},
                    {"signature", hex(signature, size)},
                    {"key_id", sha256(std::string(reinterpret_cast<char *>(bytes), 32)).substr(0, 16)}}
            .dump();
    }
};
Json manifest(const std::string &id, const std::string &version, const std::string &kind = "research") {
    return {{"schema", 1},
            {"id", id},
            {"version", version},
            {"kind", kind},
            {"platform", platformId()},
            {"application", {{"minimum", "0.2.0"}, {"maximum", "0.3.0"}}},
            {"dependencies", Json::array()},
            {"description", {{"en", "Fixture"}, {"zh-CN", "测试工具箱"}}},
            {"components", Json::array({{{"kind", kind}, {"id", "fixture." + id}, {"path", "research/plugin.json"}}})}};
}
struct Entry {
    std::string name, data;
    int type = AE_IFREG;
    std::string target;
};
Package package(const std::filesystem::path &path, const Json &description, std::vector<Entry> entries = {}) {
    entries.insert(entries.begin(), {"toolbox.json", description.dump()});
    if (entries.size() == 1) {
        entries.push_back(
            {"research/plugin.json",
             Json{{"api", 1}, {"id", "fixture." + description.at("id").get<std::string>()}, {"executable", "run"}}
                 .dump()});
        entries.push_back({"research/run", "fixture executable"});
    }
    auto *output = archive_write_new();
    archive_write_set_format_pax_restricted(output);
    archive_write_add_filter_gzip(output);
    require(archive_write_open_filename(output, path.string().c_str()) == ARCHIVE_OK,
            "Could not create archive fixture");
    std::uint64_t size = 0;
    for (const auto &file : entries) {
        auto *entry = archive_entry_new();
        archive_entry_set_pathname_utf8(entry, file.name.c_str());
        archive_entry_set_filetype(entry, file.type);
        archive_entry_set_perm(entry, 0755);
        archive_entry_set_size(entry, file.type == AE_IFREG ? file.data.size() : 0);
        if (file.type == AE_IFLNK)
            archive_entry_set_symlink(entry, file.target.c_str());
        require(archive_write_header(output, entry) == ARCHIVE_OK, "Could not write fixture header");
        if (file.type == AE_IFREG) {
            require(archive_write_data(output, file.data.data(), file.data.size()) ==
                        static_cast<la_ssize_t>(file.data.size()),
                    "Could not write fixture body");
            size += file.data.size();
        }
        archive_entry_free(entry);
    }
    archive_write_close(output);
    archive_write_free(output);
    Package p;
    p.id = description.at("id");
    p.kind = description.at("kind");
    p.version = description.at("version");
    p.platform = description.at("platform");
    p.url = "https://official.invalid/fixture.package";
    p.download_size = std::filesystem::file_size(path);
    p.installed_size = size;
    p.sha256 = sha256(path);
    p.manifest = description;
    return p;
}
struct IndependentType : PackageType {
    void validate(const Package &, const std::filesystem::path &root) const override {
        require(std::filesystem::is_regular_file(root / "payload"), "Independent package type lost its payload");
    }
};
struct Files : Transport {
    std::filesystem::path source;
    bool corrupt = false;
    void download(const std::string &, const std::filesystem::path &output, std::uint64_t, std::stop_token stop,
                  const Progress &progress) override {
        if (stop.stop_requested())
            throw Error("cancelled");
        std::filesystem::copy_file(source, output);
        if (corrupt) {
            std::ofstream stream(output, std::ios::app);
            stream << 'x';
        }
        if (progress)
            progress(std::filesystem::file_size(output), std::filesystem::file_size(source));
    }
};
Json catalogData(const Package &p, std::uint64_t sequence = 1) {
    return {{"schema", 1},
            {"sequence", sequence},
            {"published", "2026-11-09T00:00:00Z"},
            {"packages", Json::array({{{"manifest", p.manifest},
                                       {"url", p.url},
                                       {"sha256", p.sha256},
                                       {"download_size", p.download_size},
                                       {"installed_size", p.installed_size}}})}};
}
} // namespace
