#include "distribution_fixture.hpp"
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const std::vector<std::string> precedence = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                                                     "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0",
                                                     "2.0.0",        "10.0.0"};
        for (std::size_t i = 1; i < precedence.size(); ++i)
            require(Version::parse(precedence[i - 1]) < Version::parse(precedence[i]),
                    "Semantic version precedence is incorrect");
        require(Version::parse("1.0.0+build.1") == Version::parse("1.0.0+build.2"),
                "Build metadata changed precedence");
        for (const auto *value : {"1.0", "01.0.0", "1.0.0-01", "1.0.0-", "999999999999999999999999999999.0.0"})
            rejected([&] { Version::parse(value); }, "version");
        QTemporaryDir directory;
        require(directory.isValid(), "Could not isolate distribution fixtures");
        const auto root = signalscope::path_from_utf8(directory.path().toStdString());
        const auto first = package(root / "first.tgz", manifest("example.research", "1.0.0"));
        Signing signing;
        const auto signedCatalog = signing.sign(catalogData(first));
        const auto catalog = Catalog::verify(signedCatalog, signing.publicKey);
        Environment environment{applicationVersion(), platformId(), {}};
        require(catalog.latest(first.id, environment) && catalog.latest(first.id, environment)->version == "1.0.0",
                "Compatible catalog entry was not offered");
        auto altered = Json::parse(signedCatalog);
        altered["payload"] = catalogData(first, 2).dump();
        rejected([&] { Catalog::verify(altered.dump(), signing.publicKey); }, "signature");
        Signing other;
        rejected([&] { Catalog::verify(signedCatalog, other.publicKey); }, "signature");
        auto duplicated = catalogData(first);
        duplicated["packages"].push_back(duplicated["packages"][0]);
        rejected([&] { Catalog::verify(signing.sign(duplicated), signing.publicKey); }, "catalog");
        Store store(root / "state");
        store.rememberCatalog(signedCatalog, catalog);
        store.rememberCatalog(Json::parse(signedCatalog).dump(2), catalog);
        require(Catalog::verify(store.cachedCatalog(), signing.publicKey).sequence == 1,
                "Cached signed catalog cannot be reused");
        const auto newer = signing.sign(catalogData(first, 2));
        store.rememberCatalog(newer, Catalog::verify(newer, signing.publicKey));
        rejected([&] { store.rememberCatalog(signedCatalog, catalog); }, "catalog_rollback");
        Files transport;
        transport.source = root / "first.tgz";
        Store fullDisk(root / "full-disk", standardPackageTypes(), [](const auto &) { return 0; });
        rejected([&] { fullDisk.download(first, transport); }, "space");
        rejected([&] { fullDisk.install(first, root / "first.tgz", environment); }, "space");
        require(fullDisk.installedVersions().empty(), "Disk shortage changed package state");
        {
            ActivationGuard guard(store.root());
            rejected([&] { store.install(first, root / "first.tgz", environment); }, "busy");
        }
        const auto downloaded = store.download(first, transport);
        {
            std::ofstream damaged(downloaded, std::ios::binary);
            damaged << "damaged cache";
        }
        require(sha256(store.download(first, transport)) == first.sha256,
                "A damaged cache entry could not be recovered");
        store.install(first, downloaded, environment);
        require(store.components("research").size() == 1 && store.installedVersions().at(first.id) == "1.0.0",
                "Installed package did not expose its runtime components");
        store.setEnabled(first.id, false);
        require(store.components("research").empty(), "Disabled package still exposes components");
        store.setEnabled(first.id, true);
        const auto second = package(root / "second.tgz", manifest(first.id, "1.1.0"));
        store.install(second, root / "second.tgz", environment);
        require(store.installedVersions().at(first.id) == "1.1.0",
                "Package update did not become desired active version");
        store.rollback(first.id);
        require(store.installedVersions().at(first.id) == "1.0.0", "Package rollback failed");
        const auto before = store.state();
        auto badHash = second;
        badHash.sha256 = std::string(64, '0');
        rejected([&] { store.install(badHash, root / "second.tgz", environment); }, "hash");
        require(store.state() == before, "Failed update changed installation state");
        auto wrongPlatform = second;
        wrongPlatform.platform = "other-platform";
        rejected([&] { store.install(wrongPlatform, root / "second.tgz", environment); }, "platform");
        for (const auto &path :
             std::vector<std::string>{"../escape", "/absolute", "C:/absolute", "data/../../escape", "con.txt"}) {
            const auto malicious =
                package(root / "malicious.tgz", manifest("evil.package", "1.0.0"), {{path, "owned"}});
            rejected([&] { store.install(malicious, root / "malicious.tgz", environment); }, "path");
        }
        rejected([&] { checkedRelative(root, "data\\escape"); }, "path");
        require(argc == 2, "Raw hostile archive fixture missing");
        const auto hostilePath = signalscope::path_from_utf8(argv[1]);
        auto rawHostile = second;
        rawHostile.sha256 = sha256(hostilePath);
        rawHostile.download_size = std::filesystem::file_size(hostilePath);
        rawHostile.installed_size = 5;
        rejected([&] { store.install(rawHostile, hostilePath, environment); }, "path");
        const auto link =
            package(root / "link.tgz", manifest("evil.package", "1.0.0"), {{"link", "", AE_IFLNK, "../escape"}});
        rejected([&] { store.install(link, root / "link.tgz", environment); }, "path");
        auto undersized = second;
        undersized.installed_size = 1;
        rejected([&] { store.install(undersized, root / "second.tgz", environment); }, "expanded_size");
        auto dependencyManifest = manifest("dependent.research", "1.0.0");
        dependencyManifest["dependencies"] =
            Json::array({{{"id", first.id}, {"minimum", "1.0.0"}, {"maximum", "2.0.0"}}});
        const auto dependent = package(root / "dependent.tgz", dependencyManifest);
        environment.installed = store.installedVersions();
        store.install(dependent, root / "dependent.tgz", environment);
        rejected([&] { store.uninstall(first.id); }, "dependency");
        rejected([&] { store.setEnabled(first.id, false); }, "dependency");
        const auto incompatible = package(root / "major.tgz", manifest(first.id, "2.0.0"));
        require(!store.assessInstall(incompatible, environment).compatible,
                "Reverse dependency conflict was not found before download");
        rejected([&] { store.install(incompatible, root / "major.tgz", environment); }, "dependency");
        store.uninstall(dependent.id);
        store.uninstall(first.id);
        require(store.components("research").empty(), "Uninstalled modules remained active");
        auto types = standardPackageTypes();
        types["independent"] = std::make_shared<IndependentType>();
        Store extended(root / "extended", types);
        auto independentManifest = manifest("independent.toolbox", "1.0.0", "independent");
        independentManifest["components"] =
            Json::array({{{"kind", "independent"}, {"id", "independent.module"}, {"path", "payload"}}});
        const auto independent =
            package(root / "independent.tgz", independentManifest, {{"payload", "Independent package type"}});
        extended.install(independent, root / "independent.tgz", environment);
        require(extended.components("independent").size() == 1, "New package type required changing the installer");
        auto restrictedManifest = manifest("restricted.toolbox", "1.0.0");
        restrictedManifest["actions"] = Json::array({"update"});
        const auto restricted = package(root / "restricted.tgz", restrictedManifest);
        extended.install(restricted, root / "restricted.tgz", environment);
        rejected([&] { extended.setEnabled(restricted.id, false); }, "action");
        rejected([&] { extended.uninstall(restricted.id); }, "action");
        const auto retainedRoot = extended.components("research").front().root;
        writeJsonAtomically(extended.root() / "application/current.json",
                            {{"root", signalscope::path_to_utf8(retainedRoot)}});
        auto empty = extended.state();
        empty["installed"] = Json::object();
        writeJsonAtomically(extended.root() / "registry.json", empty);
        extended.collectUnused();
        require(std::filesystem::exists(retainedRoot),
                "Current application pointer was not protected by garbage collection");
        store.clearCache();
        require(!std::filesystem::exists(downloaded), "Package cache was not cleared");
        std::stop_source cancellation;
        cancellation.request_stop();
        rejected([&] { store.download(first, transport, cancellation.get_token()); }, "cancelled");
        std::cout << "Signed catalogs, versions, transactional packages, compatibility, hostile archives, rollback and "
                     "independent package types passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
