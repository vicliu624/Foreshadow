#include "distribution_config.hpp"
#include "signalscope/backend/distribution/distribution.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <openssl/evp.h>
#include <regex>
#include <set>
#include <sstream>

namespace signalscope::distribution {
namespace {
template <class T, auto Free> using Handle = std::unique_ptr<T, decltype(Free)>;
std::vector<std::string> split(const std::string &text, char separator) {
    std::vector<std::string> result;
    std::size_t start = 0;
    do {
        const auto at = text.find(separator, start);
        result.push_back(text.substr(start, at - start));
        if (at == std::string::npos)
            break;
        start = at + 1;
    } while (true);
    return result;
}
std::vector<unsigned char> unhex(const std::string &text) {
    if (text.size() % 2)
        throw Error("signature");
    std::vector<unsigned char> bytes;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned int value = 0;
        const auto result = std::from_chars(text.data() + i, text.data() + i + 2, value, 16);
        if (result.ec != std::errc{} || result.ptr != text.data() + i + 2)
            throw Error("signature");
        bytes.push_back(static_cast<unsigned char>(value));
    }
    return bytes;
}
std::string digest(EVP_MD_CTX *context) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(context, bytes.data(), &size) != 1)
        throw Error("hash");
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned int i = 0; i < size; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}
std::uint64_t number(const std::string &text) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw Error("version", {{"version", text}});
    return value;
}
bool numeric(const std::string &value) {
    return !value.empty() && value.find_first_not_of("0123456789") == std::string::npos;
}
bool inRange(const std::string &value, const Json &range) {
    const auto v = Version::parse(value);
    return (!range.contains("minimum") || v >= Version::parse(range.at("minimum"))) &&
           (!range.contains("maximum") || v < Version::parse(range.at("maximum")));
}
} // namespace
Version Version::parse(const std::string &text) {
    static const std::regex pattern(
        R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$)");
    std::smatch match;
    if (text.size() > 128 || !std::regex_match(text, match, pattern))
        throw Error("version", {{"version", text}});
    Version result{number(match[1]), number(match[2]), number(match[3]), match[4]};
    for (const auto &part : split(result.prerelease, '.'))
        if (numeric(part) && part.size() > 1 && part.front() == '0')
            throw Error("version", {{"version", text}});
    return result;
}
std::strong_ordering Version::operator<=>(const Version &other) const {
    if (auto order = major <=> other.major; order != 0)
        return order;
    if (auto order = minor <=> other.minor; order != 0)
        return order;
    if (auto order = patch <=> other.patch; order != 0)
        return order;
    if (prerelease.empty() != other.prerelease.empty())
        return prerelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    const auto left = split(prerelease, '.'), right = split(other.prerelease, '.');
    for (std::size_t i = 0; i < std::min(left.size(), right.size()); ++i) {
        const bool ln = numeric(left[i]), rn = numeric(right[i]);
        if (ln != rn)
            return ln ? std::strong_ordering::less : std::strong_ordering::greater;
        if (ln) {
            if (auto order = left[i].size() <=> right[i].size(); order != 0)
                return order;
        }
        if (auto order = left[i] <=> right[i]; order != 0)
            return order;
    }
    return left.size() <=> right.size();
}
std::string sha256(const std::string &bytes) {
    Handle<EVP_MD_CTX, EVP_MD_CTX_free> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) != 1)
        throw Error("hash");
    return digest(context.get());
}
std::string sha256(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw Error("file", {{"path", path_to_utf8(path)}});
    Handle<EVP_MD_CTX, EVP_MD_CTX_free> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
        throw Error("hash");
    std::array<char, 65536> block{};
    while (input.read(block.data(), block.size()) || input.gcount())
        if (EVP_DigestUpdate(context.get(), block.data(), static_cast<std::size_t>(input.gcount())) != 1)
            throw Error("hash");
    if (!input.eof())
        throw Error("file", {{"path", path_to_utf8(path)}});
    return digest(context.get());
}
Catalog Catalog::verify(const std::string &envelope, const std::string &publicKeyHex) {
    try {
        if (envelope.size() > 8 * 1024 * 1024)
            throw Error("catalog");
        const auto signedData = Json::parse(envelope);
        if (signedData.at("schema") != 1 || !signedData.at("payload").is_string())
            throw Error("catalog");
        const auto payload = signedData.at("payload").get<std::string>();
        const auto key = unhex(publicKeyHex);
        const auto signature = unhex(signedData.at("signature"));
        if (key.size() != 32 || signature.size() != 64 ||
            signedData.at("key_id") != sha256(std::string(key.begin(), key.end())).substr(0, 16))
            throw Error("signature");
        Handle<EVP_PKEY, EVP_PKEY_free> pkey(
            EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key.data(), key.size()), EVP_PKEY_free);
        Handle<EVP_MD_CTX, EVP_MD_CTX_free> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (!pkey || !context || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, pkey.get()) != 1 ||
            EVP_DigestVerify(context.get(), signature.data(), signature.size(),
                             reinterpret_cast<const unsigned char *>(payload.data()), payload.size()) != 1)
            throw Error("signature");
        const auto data = Json::parse(payload);
        if (data.at("schema") != 1 || !data.at("sequence").is_number_unsigned() || data.at("packages").size() > 2048)
            throw Error("catalog");
        Catalog catalog;
        catalog.sequence = data.at("sequence");
        catalog.published = data.at("published");
        std::set<std::string> seen;
        for (const auto &entry : data.at("packages")) {
            const auto manifest = entry.at("manifest");
            Package p;
            p.id = manifest.at("id");
            p.kind = manifest.at("kind");
            p.version = manifest.at("version");
            p.platform = manifest.at("platform");
            p.url = entry.at("url");
            p.sha256 = entry.at("sha256");
            p.download_size = entry.at("download_size");
            p.installed_size = entry.at("installed_size");
            p.manifest = manifest;
            p.description = manifest.value("description", Json::object());
            p.notes = manifest.value("release_notes", Json::object());
            static const std::regex identifier("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$");
            Version::parse(p.version);
            const auto validateRange = [](const Json &range) {
                if (!range.is_object())
                    throw Error("catalog");
                for (const auto *key : {"minimum", "maximum"})
                    if (range.contains(key))
                        Version::parse(range.at(key));
                if (range.contains("minimum") && range.contains("maximum") &&
                    Version::parse(range.at("minimum")) >= Version::parse(range.at("maximum")))
                    throw Error("catalog");
            };
            validateRange(manifest.value("application", Json::object()));
            const auto dependencies = manifest.value("dependencies", Json::array());
            const auto components = manifest.value("components", Json::array());
            const auto actions = manifest.value("actions", Json::array());
            if (!dependencies.is_array() || !components.is_array() || !actions.is_array() ||
                !std::regex_match(p.kind, identifier) || p.platform.empty())
                throw Error("catalog");
            for (const auto &dependency : dependencies) {
                validateRange(dependency);
                if (!std::regex_match(dependency.at("id").get<std::string>(), identifier))
                    throw Error("catalog");
            }
            for (const auto &component : components) {
                if (!component.at("id").is_string() || !component.at("kind").is_string())
                    throw Error("catalog");
                checkedRelative(std::filesystem::current_path(), component.at("path"));
            }
            for (const auto &action : actions)
                if (!action.is_string())
                    throw Error("catalog");
            if (manifest.at("schema") != 1 || !std::regex_match(p.id, identifier) || p.id.size() > 128 ||
                p.url.rfind("https://", 0) != 0 || p.sha256.size() != 64 || unhex(p.sha256).size() != 32 ||
                p.download_size == 0 || p.download_size > 512ull * 1024 * 1024 || p.installed_size == 0 ||
                p.installed_size > 2ull * 1024 * 1024 * 1024 ||
                !seen.insert(p.id + '\n' + p.version + '\n' + p.platform).second)
                throw Error("catalog");
            catalog.packages.push_back(std::move(p));
        }
        return catalog;
    } catch (const Error &) {
        throw;
    } catch (const std::exception &e) {
        throw Error("catalog", {{"detail", e.what()}});
    }
}
Catalog Catalog::official(const std::string &envelope) {
    return verify(envelope, Json::parse(build::trust).at("public_key_hex"));
}
Compatibility compatible(const Package &p, const Environment &env) {
    if (p.platform != "any" && p.platform != env.platform)
        return {false, "platform", {{"platform", p.platform}}};
    if (!inRange(env.application_version, p.manifest.value("application", Json::object())))
        return {false, "application", {{"version", env.application_version}}};
    for (const auto &dependency : p.manifest.value("dependencies", Json::array())) {
        const auto id = dependency.at("id").get<std::string>();
        const auto found = env.installed.find(id);
        if (found == env.installed.end() || !inRange(found->second, dependency))
            return {false, "dependency", {{"dependency", id}}};
    }
    return {};
}
const Package *Catalog::latest(const std::string &id, const Environment &env) const {
    const Package *result = nullptr;
    for (const auto &p : packages)
        if (p.id == id && compatible(p, env).compatible && Version::parse(p.version).prerelease.empty() &&
            (!result || Version::parse(p.version) > Version::parse(result->version)))
            result = &p;
    return result;
}
std::string officialCatalogUrl() { return Json::parse(build::trust).at("catalog_url"); }
std::string applicationPackageId() { return Json::parse(build::trust).at("application_id"); }
Authority officialAuthority() {
    const auto trust = Json::parse(build::trust);
    return {trust.at("catalog_url"), trust.at("public_key_hex")};
}
std::string applicationVersion() { return std::string(build::version); }
std::string platformId() { return std::string(build::platform); }
std::filesystem::path defaultRoot() {
    if (const auto p = environment_utf8("SIGNALSCOPE_DISTRIBUTION_ROOT"))
        return path_from_utf8(*p);
#ifdef _WIN32
    if (const auto p = environment_utf8("LOCALAPPDATA"))
        return path_from_utf8(*p) / "SignalScope" / "distribution";
#else
    if (const auto p = environment_utf8("XDG_DATA_HOME"))
        return path_from_utf8(*p) / "SignalScope" / "distribution";
#endif
    if (const auto p = environment_utf8("HOME"))
        return path_from_utf8(*p) / ".local/share/SignalScope/distribution";
    throw Error("home");
}
} // namespace signalscope::distribution
