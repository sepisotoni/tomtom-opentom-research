#include "Driver.h"

#include "ControlChannel.h"
#include "../src/display/idd_ipc.h"
#include "../src/display/frame_converter.h"
#include "../src/display/frame_transport.h"

#include <avrt.h>
#include <array>
#include <chrono>
#include <new>

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kMonitorWidth = 320;
constexpr DWORD kMonitorHeight = 240;
constexpr DWORD kMonitorRefreshHz = 30;
constexpr wchar_t kMonitorName[] = L"TomTom USB Display";
constexpr wchar_t kManufacturerName[] = L"TomTom Face Studio";
constexpr wchar_t kModelName[] = L"TomTom 320x240";

struct DeviceContextWrapper {
    tt::idd::AdapterContext* context = nullptr;
};

struct MonitorContextWrapper {
    tt::idd::MonitorContext* context = nullptr;
};

WDF_DECLARE_CONTEXT_TYPE(DeviceContextWrapper);
WDF_DECLARE_CONTEXT_TYPE(MonitorContextWrapper);

void fill_signal_info(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& signal,
                      DWORD width, DWORD height, DWORD refresh,
                      bool monitor_mode) noexcept {
    signal = {};
    signal.totalSize.cx = signal.activeSize.cx = width;
    signal.totalSize.cy = signal.activeSize.cy = height;
    signal.AdditionalSignalInfo.vSyncFreqDivider = monitor_mode ? 0 : 1;
    signal.AdditionalSignalInfo.videoStandard = 255;
    signal.vSyncFreq.Numerator = refresh;
    signal.vSyncFreq.Denominator = 1;
    signal.hSyncFreq.Numerator = refresh * height;
    signal.hSyncFreq.Denominator = 1;
    signal.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    signal.pixelRate = static_cast<UINT64>(refresh) * width * height;
}

IDDCX_MONITOR_MODE make_monitor_mode() noexcept {
    IDDCX_MONITOR_MODE mode{};
    mode.Size = sizeof(mode);
    mode.Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER;
    fill_signal_info(mode.MonitorVideoSignalInfo, kMonitorWidth,
                     kMonitorHeight, kMonitorRefreshHz, true);
    return mode;
}

IDDCX_TARGET_MODE make_target_mode() noexcept {
    IDDCX_TARGET_MODE mode{};
    mode.Size = sizeof(mode);
    fill_signal_info(mode.TargetVideoSignalInfo.targetVideoSignalInfo,
                     kMonitorWidth, kMonitorHeight, kMonitorRefreshHz, false);
    return mode;
}

}  // namespace

namespace tt::idd {

class Direct3DDevice {
public:
    explicit Direct3DDevice(LUID adapter_luid) : adapter_luid_(adapter_luid) {}

    HRESULT initialize() noexcept {
        auto result = CreateDXGIFactory2(
            0, IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf()));
        if (FAILED(result)) {
            return result;
        }
        result = factory_->EnumAdapterByLuid(
            adapter_luid_, IID_PPV_ARGS(adapter_.ReleaseAndGetAddressOf()));
        if (FAILED(result)) {
            return result;
        }
        return D3D11CreateDevice(
            adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
            D3D11_SDK_VERSION, device_.ReleaseAndGetAddressOf(), nullptr,
            context_.ReleaseAndGetAddressOf());
    }

    ID3D11Device* device() const noexcept { return device_.Get(); }
    ID3D11DeviceContext* context() const noexcept { return context_.Get(); }

private:
    LUID adapter_luid_{};
    ComPtr<IDXGIFactory5> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
};

class SwapChainProcessor {
public:
    SwapChainProcessor(IDDCX_SWAPCHAIN swap_chain,
                       std::shared_ptr<Direct3DDevice> device,
                       std::shared_ptr<ControlChannel> control,
                       HANDLE frame_event)
        : swap_chain_(swap_chain), device_(std::move(device)),
          control_(std::move(control)),
          frame_event_(frame_event), stop_event_(CreateEventW(nullptr, TRUE,
                                                               FALSE, nullptr)) {
        if (stop_event_ && transport_.start()) {
            thread_ = CreateThread(nullptr, 0, thread_entry, this, 0, nullptr);
            if (thread_ && control_) {
                // From here the control channel pauses/resumes this transport (and releases the
                // receiver's TCP connection) whenever the Studio holds its mirror lease or the user
                // ran `TomTomDisplayControl.exe pause`.
                control_->attach(&transport_);
            }
        }
    }

    ~SwapChainProcessor() {
        if (stop_event_) {
            SetEvent(stop_event_);
        }
        if (thread_) {
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        if (control_) {
            control_->detach(&transport_);
        }
        if (stop_event_) {
            CloseHandle(stop_event_);
        }
        if (swap_chain_) {
            WdfObjectDelete(reinterpret_cast<WDFOBJECT>(swap_chain_));
        }
    }

    bool valid() const noexcept { return thread_ != nullptr; }

private:
    static DWORD WINAPI thread_entry(void* argument) noexcept {
        static_cast<SwapChainProcessor*>(argument)->run();
        return 0;
    }

    void run() noexcept {
        DWORD task_index = 0;
        HANDLE task = AvSetMmThreadCharacteristicsW(L"Distribution",
                                                     &task_index);
        ComPtr<IDXGIDevice> dxgi_device;
        auto result = device_->device()->QueryInterface(
            IID_PPV_ARGS(dxgi_device.ReleaseAndGetAddressOf()));
        if (SUCCEEDED(result)) {
            IDARG_IN_SWAPCHAINSETDEVICE set_device{};
            set_device.pDevice = dxgi_device.Get();
            result = IddCxSwapChainSetDevice(swap_chain_, &set_device);
        }
        if (SUCCEEDED(result)) {
            consume_frames();
        } else {
            OutputDebugStringW(
                L"TomTom IDD: failed to attach the Direct3D device to the swap chain.\n");
        }
        transport_.stop();
        if (swap_chain_) {
            WdfObjectDelete(reinterpret_cast<WDFOBJECT>(swap_chain_));
            swap_chain_ = nullptr;
        }
        if (task) {
            AvRevertMmThreadCharacteristics(task);
        }
    }

    HRESULT get_staging_texture(ID3D11Texture2D* source,
                                ID3D11Texture2D** staging) noexcept {
        D3D11_TEXTURE2D_DESC source_desc{};
        source->GetDesc(&source_desc);
        if (source_desc.Width == 0 || source_desc.Height == 0 ||
            source_desc.Width > 16384 || source_desc.Height > 16384 ||
            source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            return E_INVALIDARG;
        }
        if (staging_ && staging_width_ == source_desc.Width &&
            staging_height_ == source_desc.Height) {
            *staging = staging_.Get();
            return S_OK;
        }

        staging_.Reset();
        source_desc.Usage = D3D11_USAGE_STAGING;
        source_desc.BindFlags = 0;
        source_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        source_desc.MiscFlags = 0;
        auto result = device_->device()->CreateTexture2D(
            &source_desc, nullptr, staging_.ReleaseAndGetAddressOf());
        if (FAILED(result)) {
            return result;
        }
        staging_width_ = source_desc.Width;
        staging_height_ = source_desc.Height;
        *staging = staging_.Get();
        return S_OK;
    }

    void capture_frame(ID3D11Texture2D* source) noexcept {
        ID3D11Texture2D* staging = nullptr;
        if (FAILED(get_staging_texture(source, &staging))) {
            report_capture_error(
                L"TomTom IDD: unsupported swap-chain surface format or size.\n");
            return;
        }
        device_->context()->CopyResource(staging, source);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(device_->context()->Map(staging, 0, D3D11_MAP_READ, 0,
                                           &mapped))) {
            report_capture_error(
                L"TomTom IDD: could not map the staged frame for reading.\n");
            return;
        }
        const auto source_desc = [staging] {
            D3D11_TEXTURE2D_DESC desc{};
            staging->GetDesc(&desc);
            return desc;
        }();
        const auto source_bytes =
            static_cast<std::size_t>(mapped.RowPitch) * source_desc.Height;
        const bool converted = tt::display::convert_bgra8_to_rgb565(
            static_cast<const std::uint8_t*>(mapped.pData), source_bytes,
            source_desc.Width, source_desc.Height, mapped.RowPitch,
            frame_.data(), frame_.size());
        device_->context()->Unmap(staging, 0);
        if (converted) {
            // Keep the converted frame fresh even while streaming is gated (it is only 150 KB),
            // so the display can be refreshed the moment the gate opens again.
            have_frame_ = true;
            if (gate_allows()) {
                submit_current_frame();
            }
        } else {
            report_capture_error(
                L"TomTom IDD: BGRA-to-RGB565 frame conversion failed.\n");
        }
    }

    bool gate_allows() const noexcept {
        return !control_ || control_->streaming_allowed();
    }

    void submit_current_frame() noexcept {
        last_submit_ = std::chrono::steady_clock::now();
        if (transport_.submit_frame(frame_.data(), frame_.size())) {
            capture_error_reported_ = false;
        } else {
            report_capture_error(
                L"TomTom IDD: the frame transport is not running.\n");
        }
    }

    // Called every loop iteration and on a timer: when the gate re-opens (Studio mirror stopped, or
    // `resume`), push the last frame at once. A static desktop produces no new swap-chain frames, so
    // without this the TomTom would keep showing the Studio's last mirror frame.
    //
    // It is also the keepalive: while streaming is allowed, the last frame is re-sent once a second even
    // if the desktop is static. That keeps the receiver's 2 s idle timeout from dropping the connection,
    // repaints a receiver that was restarted, and keeps any device-side "no frames -> restore the clock"
    // watchdog from firing on an idle desktop. About 150 KB/s over USB; the interval must stay below
    // half of whatever that watchdog's timeout turns out to be (see docs/AGENT_BOARD.md).
    // The decision itself is tt::idd_ipc::should_resend_last_frame (unit-tested).
    void service_gate() noexcept {
        const bool allowed = gate_allows();
        if (tt::idd_ipc::should_resend_last_frame(
                allowed, have_frame_, last_gate_allowed_,
                std::chrono::steady_clock::now() - last_submit_)) {
            submit_current_frame();
        }
        last_gate_allowed_ = allowed;
    }

    void report_capture_error(const wchar_t* message) noexcept {
        if (!capture_error_reported_) {
            OutputDebugStringW(message);
            capture_error_reported_ = true;
        }
    }

    void consume_frames() noexcept {
        auto next_capture = std::chrono::steady_clock::now();
        for (;;) {
            service_gate();
            IDARG_OUT_RELEASEANDACQUIREBUFFER buffer{};
            auto result = IddCxSwapChainReleaseAndAcquireBuffer(swap_chain_,
                                                                &buffer);
            if (result == E_PENDING) {
                HANDLE events[] = {stop_event_, frame_event_};
                // Wake periodically (not INFINITE) so a gate that re-opens on a static desktop is noticed.
                const auto wait = WaitForMultipleObjects(
                    ARRAYSIZE(events), events, FALSE, kGateRecheckMs);
                if (wait == WAIT_OBJECT_0) {
                    break;
                }
                if (wait == WAIT_OBJECT_0 + 1 || wait == WAIT_TIMEOUT) {
                    continue;
                }
                break;
            }
            if (FAILED(result)) {
                break;
            }

            ComPtr<IDXGIResource> acquired;
            acquired.Attach(buffer.MetaData.pSurface);
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_capture) {
                ComPtr<ID3D11Texture2D> texture;
                if (SUCCEEDED(acquired.As(&texture))) {
                    capture_frame(texture.Get());
                }
                next_capture = now + std::chrono::milliseconds(100);
            }
            acquired.Reset();
            if (FAILED(IddCxSwapChainFinishedProcessingFrame(swap_chain_))) {
                OutputDebugStringW(
                    L"TomTom IDD: IddCx frame completion failed.\n");
                break;
            }
        }
    }

    static constexpr DWORD kGateRecheckMs = 250;

    IDDCX_SWAPCHAIN swap_chain_;
    std::shared_ptr<Direct3DDevice> device_;
    std::shared_ptr<ControlChannel> control_;
    HANDLE frame_event_;
    HANDLE stop_event_;
    HANDLE thread_ = nullptr;
    ComPtr<ID3D11Texture2D> staging_;
    UINT staging_width_ = 0;
    UINT staging_height_ = 0;
    bool capture_error_reported_ = false;
    bool have_frame_ = false;
    bool last_gate_allowed_ = true;
    std::chrono::steady_clock::time_point last_submit_{};
    std::array<std::uint8_t, tt::display::kFrameBytes> frame_{};
    tt::display::FrameTransport transport_;
};

}  // namespace tt::idd

namespace tt::idd {

AdapterContext::AdapterContext(WDFDEVICE device) noexcept
    : device_(device), adapter_(nullptr) {}

AdapterContext::~AdapterContext() = default;

NTSTATUS AdapterContext::initialize_adapter() noexcept {
    if (!control_) {
        // May run again on a later D0 entry; the channel is created once per adapter object.
        try {
            auto channel = std::make_shared<ControlChannel>();
            if (channel->start()) {
                control_ = std::move(channel);
            } else {
                OutputDebugStringW(
                    L"TomTom IDD: control channel did not start; streaming is not gated.\n");
            }
        } catch (const std::bad_alloc&) {
            OutputDebugStringW(
                L"TomTom IDD: out of memory creating the control channel.\n");
        }
    }

    IDDCX_ADAPTER_CAPS caps{};
    caps.Size = sizeof(caps);
    caps.MaxMonitorsSupported = 1;
    caps.EndPointDiagnostics.Size = sizeof(caps.EndPointDiagnostics);
    caps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    caps.EndPointDiagnostics.TransmissionType =
        IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
    caps.EndPointDiagnostics.pEndPointFriendlyName = kMonitorName;
    caps.EndPointDiagnostics.pEndPointManufacturerName = kManufacturerName;
    caps.EndPointDiagnostics.pEndPointModelName = kModelName;

    IDDCX_ENDPOINT_VERSION version{};
    version.Size = sizeof(version);
    version.MajorVer = 1;
    caps.EndPointDiagnostics.pFirmwareVersion = &version;
    caps.EndPointDiagnostics.pHardwareVersion = &version;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,
                                             DeviceContextWrapper);
    IDARG_IN_ADAPTER_INIT input{};
    input.WdfDevice = device_;
    input.pCaps = &caps;
    input.ObjectAttributes = &attributes;
    IDARG_OUT_ADAPTER_INIT output{};
    const auto status = IddCxAdapterInitAsync(&input, &output);
    if (NT_SUCCESS(status)) {
        adapter_ = output.AdapterObject;
        WdfObjectGet_DeviceContextWrapper(
            reinterpret_cast<WDFOBJECT>(adapter_))->context = this;
    }
    return status;
}

void AdapterContext::report_monitor() noexcept {
    if (!adapter_) {
        return;
    }
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,
                                             MonitorContextWrapper);
    IDDCX_MONITOR_INFO info{};
    info.Size = sizeof(info);
    info.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
    info.ConnectorIndex = 0;
    info.MonitorDescription.Size = sizeof(info.MonitorDescription);
    info.MonitorDescription.Type =
        IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    info.MonitorDescription.DataSize = 0;
    info.MonitorDescription.pData = nullptr;
    if (FAILED(CoCreateGuid(&info.MonitorContainerId))) {
        OutputDebugStringW(
            L"TomTom IDD: could not create the monitor container ID.\n");
        return;
    }

    IDARG_IN_MONITORCREATE input{};
    input.ObjectAttributes = &attributes;
    input.pMonitorInfo = &info;
    IDARG_OUT_MONITORCREATE output{};
    attributes.EvtCleanupCallback = [](WDFOBJECT object) {
        auto* context = WdfObjectGet_MonitorContextWrapper(object);
        delete context->context;
        context->context = nullptr;
    };
    auto status = IddCxMonitorCreate(adapter_, &input, &output);
    if (!NT_SUCCESS(status)) {
        OutputDebugStringW(L"TomTom IDD: IddCxMonitorCreate failed.\n");
        return;
    }
    auto* wrapper = WdfObjectGet_MonitorContextWrapper(
        reinterpret_cast<WDFOBJECT>(output.MonitorObject));
    wrapper->context = nullptr;
    wrapper->context =
        new (std::nothrow) MonitorContext(output.MonitorObject, control_);
    if (!wrapper->context) {
        WdfObjectDelete(reinterpret_cast<WDFOBJECT>(output.MonitorObject));
        return;
    }
    IDARG_OUT_MONITORARRIVAL arrival{};
    status = IddCxMonitorArrival(output.MonitorObject, &arrival);
    if (NT_SUCCESS(status)) {
        wrapper->context->set_arrived();
    } else {
        OutputDebugStringW(L"TomTom IDD: IddCxMonitorArrival failed.\n");
        WdfObjectDelete(reinterpret_cast<WDFOBJECT>(output.MonitorObject));
    }
}

MonitorContext::MonitorContext(IDDCX_MONITOR monitor,
                               std::shared_ptr<ControlChannel> control) noexcept
    : monitor_(monitor), control_(std::move(control)) {}

MonitorContext::~MonitorContext() {
    processor_.reset();
    if (arrived_) {
        const auto status = IddCxMonitorDeparture(monitor_);
        if (!NT_SUCCESS(status)) {
            OutputDebugStringW(
                L"TomTom IDD: IddCxMonitorDeparture failed.\n");
        }
    }
}

void MonitorContext::set_arrived() noexcept {
    arrived_ = true;
}

NTSTATUS MonitorContext::assign_swap_chain(IDDCX_SWAPCHAIN swap_chain,
                                           LUID adapter_luid,
                                           HANDLE frame_event) noexcept {
    processor_.reset();
    try {
        auto device = std::make_shared<Direct3DDevice>(adapter_luid);
        if (FAILED(device->initialize())) {
            WdfObjectDelete(reinterpret_cast<WDFOBJECT>(swap_chain));
            OutputDebugStringW(
                L"TomTom IDD: Direct3D device initialization failed.\n");
            return STATUS_UNSUCCESSFUL;
        }
        auto processor = std::make_unique<SwapChainProcessor>(
            swap_chain, std::move(device), control_, frame_event);
        if (processor->valid()) {
            processor_ = std::move(processor);
            return STATUS_SUCCESS;
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    } catch (const std::bad_alloc&) {
        WdfObjectDelete(reinterpret_cast<WDFOBJECT>(swap_chain));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
}

void MonitorContext::unassign_swap_chain() noexcept {
    processor_.reset();
}

}  // namespace tt::idd

extern "C" DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD on_device_add;
EVT_WDF_DEVICE_D0_ENTRY on_device_d0_entry;
EVT_IDD_CX_ADAPTER_INIT_FINISHED on_adapter_init_finished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES on_commit_modes;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES on_get_default_modes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES on_query_target_modes;
EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN on_assign_swap_chain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN on_unassign_swap_chain;

extern "C" BOOL WINAPI DllMain(HINSTANCE, UINT, LPVOID) {
    return TRUE;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object,
                                PUNICODE_STRING registry_path) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, on_device_add);
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    return WdfDriverCreate(driver_object, registry_path, &attributes, &config,
                           WDF_NO_HANDLE);
}

_Use_decl_annotations_
NTSTATUS on_device_add(WDFDRIVER, PWDFDEVICE_INIT device_init) {
    WDF_PNPPOWER_EVENT_CALLBACKS power_callbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power_callbacks);
    power_callbacks.EvtDeviceD0Entry = on_device_d0_entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(device_init, &power_callbacks);

    IDD_CX_CLIENT_CONFIG idd_config;
    IDD_CX_CLIENT_CONFIG_INIT(&idd_config);
    idd_config.EvtIddCxAdapterInitFinished = on_adapter_init_finished;
    idd_config.EvtIddCxAdapterCommitModes = on_commit_modes;
    idd_config.EvtIddCxMonitorGetDefaultDescriptionModes =
        on_get_default_modes;
    idd_config.EvtIddCxMonitorQueryTargetModes = on_query_target_modes;
    idd_config.EvtIddCxMonitorAssignSwapChain = on_assign_swap_chain;
    idd_config.EvtIddCxMonitorUnassignSwapChain = on_unassign_swap_chain;
    auto status = IddCxDeviceInitConfig(device_init, &idd_config);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,
                                             DeviceContextWrapper);
    attributes.EvtCleanupCallback = [](WDFOBJECT object) {
        auto* wrapper = WdfObjectGet_DeviceContextWrapper(object);
        delete wrapper->context;
        wrapper->context = nullptr;
    };
    WDFDEVICE device = nullptr;
    status = WdfDeviceCreate(&device_init, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    WdfObjectGet_DeviceContextWrapper(
        reinterpret_cast<WDFOBJECT>(device))->context = nullptr;
    status = IddCxDeviceInitialize(device);
    if (NT_SUCCESS(status)) {
        WdfObjectGet_DeviceContextWrapper(
            reinterpret_cast<WDFOBJECT>(device))->context =
            new (std::nothrow) tt::idd::AdapterContext(device);
        if (!WdfObjectGet_DeviceContextWrapper(
                 reinterpret_cast<WDFOBJECT>(device))->context) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    return status;
}

_Use_decl_annotations_
NTSTATUS on_device_d0_entry(WDFDEVICE device, WDF_POWER_DEVICE_STATE) {
    auto* context = WdfObjectGet_DeviceContextWrapper(
        reinterpret_cast<WDFOBJECT>(device))->context;
    if (!context) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return context->initialize_adapter();
}

_Use_decl_annotations_
NTSTATUS on_adapter_init_finished(
    IDDCX_ADAPTER adapter,
    const IDARG_IN_ADAPTER_INIT_FINISHED* input) {
    auto* wrapper = WdfObjectGet_DeviceContextWrapper(
        reinterpret_cast<WDFOBJECT>(adapter));
    if (NT_SUCCESS(input->AdapterInitStatus) && wrapper->context) {
        wrapper->context->report_monitor();
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS on_commit_modes(IDDCX_ADAPTER, const IDARG_IN_COMMITMODES*) {
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS on_get_default_modes(
    IDDCX_MONITOR,
    const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* input,
    IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* output) {
    output->DefaultMonitorModeBufferOutputCount = 1;
    if (input->DefaultMonitorModeBufferInputCount == 0) {
        return STATUS_SUCCESS;
    }
    if (input->DefaultMonitorModeBufferInputCount < 1 ||
        input->pDefaultMonitorModes == nullptr) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    input->pDefaultMonitorModes[0] = make_monitor_mode();
    output->PreferredMonitorModeIdx = 0;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS on_query_target_modes(
    IDDCX_MONITOR, const IDARG_IN_QUERYTARGETMODES* input,
    IDARG_OUT_QUERYTARGETMODES* output) {
    output->TargetModeBufferOutputCount = 1;
    if (input->TargetModeBufferInputCount == 0) {
        return STATUS_SUCCESS;
    }
    if (input->TargetModeBufferInputCount < 1 ||
        input->pTargetModes == nullptr) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    input->pTargetModes[0] = make_target_mode();
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS on_assign_swap_chain(IDDCX_MONITOR monitor,
                              const IDARG_IN_SETSWAPCHAIN* input) {
    auto* wrapper = WdfObjectGet_MonitorContextWrapper(
        reinterpret_cast<WDFOBJECT>(monitor));
    if (!wrapper->context) {
        WdfObjectDelete(reinterpret_cast<WDFOBJECT>(input->hSwapChain));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return wrapper->context->assign_swap_chain(input->hSwapChain,
                                               input->RenderAdapterLuid,
                                               input->hNextSurfaceAvailable);
}

_Use_decl_annotations_
NTSTATUS on_unassign_swap_chain(IDDCX_MONITOR monitor) {
    auto* wrapper = WdfObjectGet_MonitorContextWrapper(
        reinterpret_cast<WDFOBJECT>(monitor));
    if (wrapper->context) {
        wrapper->context->unassign_swap_chain();
    }
    return STATUS_SUCCESS;
}
