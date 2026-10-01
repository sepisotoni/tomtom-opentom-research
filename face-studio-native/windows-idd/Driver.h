#pragma once

#define NOMINMAX
#include <windows.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>
#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <wrl.h>

#include <memory>

namespace tt::idd {

class SwapChainProcessor;

class AdapterContext {
public:
    explicit AdapterContext(WDFDEVICE device) noexcept;
    ~AdapterContext();

    NTSTATUS initialize_adapter() noexcept;
    void report_monitor() noexcept;

private:
    WDFDEVICE device_;
    IDDCX_ADAPTER adapter_;
};

class MonitorContext {
public:
    explicit MonitorContext(IDDCX_MONITOR monitor) noexcept;
    ~MonitorContext();

    void set_arrived() noexcept;
    NTSTATUS assign_swap_chain(IDDCX_SWAPCHAIN swap_chain, LUID adapter_luid,
                                HANDLE frame_event) noexcept;
    void unassign_swap_chain() noexcept;

private:
    IDDCX_MONITOR monitor_;
    std::unique_ptr<SwapChainProcessor> processor_;
    bool arrived_ = false;
};

}  // namespace tt::idd
