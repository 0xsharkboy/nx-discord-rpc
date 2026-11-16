#include <switch.h>

#include "drpc/client.hpp"

namespace drpc {

namespace {

class SwitchClient final : public Client {
public:
    ~SwitchClient() override { close(); }

    ConnectError connect() override {
        close();
        if (R_FAILED(smGetService(&srv_, ipc::kServiceName))) return ConnectError::NotRunning;
        open_ = true;
        uint32_t v = 0;
        if (R_FAILED(serviceDispatchOut(&srv_, static_cast<u32>(ipc::Cmd::GetProtocolVersion), v))) {
            close();
            return ConnectError::NotRunning;
        }
        version_ = v;
        if (v != ipc::kProtocolVersion) return ConnectError::VersionMismatch;
        return ConnectError::None;
    }

    uint32_t remote_version() const override { return version_; }

    bool status(ipc::Status& out) override { return get_buffer(ipc::Cmd::GetStatus, out); }
    bool link(ipc::Link& out) override { return get_buffer(ipc::Cmd::GetLink, out); }

    bool settings(ipc::Settings& out) override {
        return open_ && R_SUCCEEDED(serviceDispatchOut(&srv_, static_cast<u32>(ipc::Cmd::GetSettings), out));
    }

    bool set_settings(const ipc::Settings& in) override {
        return open_ && R_SUCCEEDED(serviceDispatchIn(&srv_, static_cast<u32>(ipc::Cmd::SetSettings), in));
    }

    bool start_link() override { return simple(ipc::Cmd::StartLink); }
    bool cancel_link() override { return simple(ipc::Cmd::CancelLink); }
    bool unlink() override { return simple(ipc::Cmd::Unlink); }
    bool forget_key() override { return simple(ipc::Cmd::ForgetKey); }

private:
    template <typename T>
    bool get_buffer(ipc::Cmd cmd, T& out) {
        if (!open_) return false;
        return R_SUCCEEDED(serviceDispatch(&srv_, static_cast<u32>(cmd),
                                           .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                                           .buffers = {{&out, sizeof out}}));
    }

    bool simple(ipc::Cmd cmd) { return open_ && R_SUCCEEDED(serviceDispatch(&srv_, static_cast<u32>(cmd))); }

    void close() {
        if (open_) serviceClose(&srv_);
        open_ = false;
    }

    Service srv_{};
    bool open_ = false;
    uint32_t version_ = 0;
};

}  // namespace

std::unique_ptr<Client> make_switch_client() { return std::make_unique<SwitchClient>(); }

}  // namespace drpc
