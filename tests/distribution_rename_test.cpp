#define NOMINMAX
#include <windows.h>
#include "distribution_fixture.hpp"
#include <chrono>
#include <thread>

namespace {
class HeldDirectory final : public PackageType {
  public:
    explicit HeldDirectory(bool releaseSoon) : releaseSoon_(releaseSoon) {}
    ~HeldDirectory() override { release(); }
    void release() const {
        if (permanent_ != INVALID_HANDLE_VALUE) {
            CloseHandle(permanent_);
            permanent_ = INVALID_HANDLE_VALUE;
        }
    }
    void validate(const Package &, const std::filesystem::path &root) const override {
        const auto handle = CreateFileW(root.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "Could not acquire native directory reader");
        if (releaseSoon_)
            releaser_ = std::jthread([handle] {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                CloseHandle(handle);
            });
        else
            permanent_ = handle;
    }
  private:
    bool releaseSoon_;
    mutable HANDLE permanent_ = INVALID_HANDLE_VALUE;
    mutable std::jthread releaser_;
};
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temporary;
        const auto root=signalscope::path_from_utf8((temporary.path()+QStringLiteral("/\u4e8b\u52a1 \U0001f4e1")).toStdString());
        std::filesystem::create_directories(root);
        // The legacy fixture writer takes a narrow output filename. Give it
        // an ASCII scratch path, then exercise the real installer with both
        // archive and destination in the native Unicode directory.
        const auto generated=signalscope::path_from_utf8(temporary.path().toStdString())/"fixture.tar.gz";
        const auto p=package(generated,manifest("org.example.locked","1.0.0","independent"),{{"payload","Independent provider data"}});
        const auto archive=root/"package.tar.gz";
        std::filesystem::copy_file(generated,archive);
        auto brief=std::make_shared<HeldDirectory>(true);
        Store transient(root/"transient",{{"independent",brief}});
        const auto start=std::chrono::steady_clock::now();
        transient.install(p,archive,{applicationVersion(),platformId(),{}});
        require(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(200),"Native lock did not exercise commit waiting");
        require(transient.state().at("installed").contains(p.id),"Released native reader prevented installation");
        auto held=std::make_shared<HeldDirectory>(false);
        Store persistent(root/"persistent",{{"independent",held}});
        const auto blocked=std::chrono::steady_clock::now();bool failed=false;
        try {persistent.install(p,archive,{applicationVersion(),platformId(),{}});}
        catch(const Error& error) {
            require(error.code=="storage" && error.arguments.contains("native_error"),"Native commit failure lost structured OS error");
            const auto failedSource=signalscope::path_from_utf8(error.arguments.at("source").get<std::string>());
            require(failedSource.parent_path()==persistent.root(),"Native commit failure lost its Unicode source path");
            failed=true;
        }
        held->release();
        require(failed,"Persistent native reader was silently accepted");
        require(std::chrono::steady_clock::now()-blocked<std::chrono::seconds(8),"Native commit retry was not bounded");
        require(persistent.state().at("installed").empty(),"Failed native commit changed registry state");
        require(persistent.state().at("revision")==0,"Failed native commit advanced the registry revision");
        std::cout<<"Native temporary and persistent directory readers preserve transaction behavior\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
