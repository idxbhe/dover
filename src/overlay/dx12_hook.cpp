#include "overlay/dx12_hook.h"
#include "overlay/overlay_ui.h"
#include "shared/input_utils.h"
#include "shared/theme.h"
#include "overlay/hook_utils.h"
#include "overlay/input_hook.h"
#include "shared/log.h"
#include "shared/renderer.h"
#include "shared/settings/app_config.h"
#include "overlay_runtime.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <vector>
#include <atomic>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

namespace dover::overlay {

void ResetDx12State(bool device_lost = false);

namespace {
constexpr int kPresentIndex = 8;
constexpr int kResizeBuffersIndex = 13;
constexpr int kExecuteCommandListsIndex = 10;
constexpr int kMaxFrameBuffers = 4;
constexpr int kMaxSrvDescriptors = 512; // 0 = ImGui Font, 1-511 = Assets

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(WINAPI*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);

PresentFn g_original_present = nullptr;
ResizeBuffersFn g_original_resize_buffers = nullptr;
ExecuteCommandListsFn g_original_execute_command_lists = nullptr;

std::atomic<bool> g_present_hooked{false};
std::atomic<bool> g_resize_hooked{false};
std::atomic<bool> g_execute_hooked{false};
std::atomic<bool> g_imgui_initialized{false};

// Raw pointers (Zero ComPtr overhead on global teardown). We explicitly manage lifetimes in ShutdownDx12Hook.
ID3D12Device* g_device = nullptr;
ID3D12CommandQueue* g_command_queue = nullptr;
ID3D12GraphicsCommandList* g_command_list = nullptr;
ID3D12DescriptorHeap* g_rtv_heap = nullptr;
ID3D12DescriptorHeap* g_srv_heap = nullptr;

UINT g_rtv_descriptor_size = 0;
UINT g_srv_descriptor_size = 0;
UINT g_buffer_count = 0;

struct FrameContext {
    ID3D12CommandAllocator* command_allocator = nullptr;
    ID3D12Resource* backbuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle{};
    UINT64 fence_value = 0;
};

FrameContext g_frame_contexts[kMaxFrameBuffers] = {};

ID3D12Fence* g_fence = nullptr;
HANDLE g_fence_event = nullptr;
UINT64 g_fence_value = 0;

struct UploadBufferEntry {
    ID3D12Resource* upload_buffer = nullptr;
    UINT64 fence_value = 0;
};
UploadBufferEntry g_deferred_uploads[kMaxSrvDescriptors];
UINT g_deferred_uploads_count = 0;

struct TextureEntry {
    void* tex_id = nullptr;
    ID3D12Resource* resource = nullptr;
    int srv_index = -1;
};
TextureEntry g_allocated_textures[kMaxSrvDescriptors];
UINT g_allocated_textures_count = 0;

void WaitForGpu() {
    if (g_command_queue && g_fence && g_fence_event) {
        g_fence_value++;
        g_command_queue->Signal(g_fence, g_fence_value);
        g_fence->SetEventOnCompletion(g_fence_value, g_fence_event);
        WaitForSingleObject(g_fence_event, INFINITE);
    }
}

void ProcessDeferredUploads() {
    if (g_deferred_uploads_count == 0 || !g_fence) return;
    
    UINT64 completed = g_fence->GetCompletedValue();
    for (UINT i = 0; i < g_deferred_uploads_count; ) {
        if (completed >= g_deferred_uploads[i].fence_value) {
            if (g_deferred_uploads[i].upload_buffer) {
                g_deferred_uploads[i].upload_buffer->Release();
            }
            g_deferred_uploads[i] = g_deferred_uploads[g_deferred_uploads_count - 1];
            g_deferred_uploads_count--;
        } else {
            ++i;
        }
    }
}

void CleanupRenderTargets() {
    WaitForGpu();
    for (UINT i = 0; i < g_buffer_count; ++i) {
        if (g_frame_contexts[i].backbuffer) {
            g_frame_contexts[i].backbuffer->Release();
            g_frame_contexts[i].backbuffer = nullptr;
        }
    }
}

bool CreateRenderTargets(IDXGISwapChain* swapchain) {
    if (!g_device || !g_rtv_heap) return false;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < g_buffer_count; ++i) {
        g_frame_contexts[i].rtv_handle = rtv_handle;
        if (SUCCEEDED(swapchain->GetBuffer(i, IID_PPV_ARGS(&g_frame_contexts[i].backbuffer)))) {
            g_device->CreateRenderTargetView(g_frame_contexts[i].backbuffer, nullptr, rtv_handle);
        }
        rtv_handle.ptr += g_rtv_descriptor_size;
    }
    return true;
}

void WINAPI HookedExecuteCommandListsInternal(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    if (!g_command_queue && queue) {
        D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_command_queue = queue;
            g_command_queue->AddRef();
            dover::shared::LogInfo("DX12: Captured DIRECT CommandQueue");
        }
    }
    if (g_original_execute_command_lists) {
        g_original_execute_command_lists(queue, NumCommandLists, ppCommandLists);
    }
}

void WINAPI HookedExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    __try {
        HookedExecuteCommandListsInternal(queue, NumCommandLists, ppCommandLists);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dover::shared::LogError("DX12 FATAL: HookedExecuteCommandLists Access Violation. Queue: %p", queue);
        if (g_original_execute_command_lists) {
            g_original_execute_command_lists(queue, NumCommandLists, ppCommandLists);
        }
    }
}

HRESULT WINAPI HookedPresentInternal(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags) {
    if (!swapchain || IsOverlayShutdownRequested()) {
        if (g_original_present) {
            return g_original_present(swapchain, sync_interval, flags);
        }
        return S_OK;
    }

    TickInputCooldown();

    if (!g_imgui_initialized.load() && g_command_queue) {
        if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&g_device)))) {
            shared::SetDx12Device(g_device);
            shared::g_CreateDx12TextureFn = &CreateDx12Texture;
            shared::g_ReleaseDx12TextureFn = &ReleaseDx12Texture;
            
            DXGI_SWAP_CHAIN_DESC desc = {};
            swapchain->GetDesc(&desc);
            HWND hwnd = desc.OutputWindow;
            g_buffer_count = desc.BufferCount;
            
            GetOverlayState().game_hwnd.store(hwnd, std::memory_order_release);
            GetOverlayState().swapchain_width.store(desc.BufferDesc.Width, std::memory_order_relaxed);
            GetOverlayState().swapchain_height.store(desc.BufferDesc.Height, std::memory_order_relaxed);
            UpdateMouseScaling();
            if (g_buffer_count > kMaxFrameBuffers) g_buffer_count = kMaxFrameBuffers;

            // Create Fences
            g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
            g_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            
            // Create RTV Heap
            D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
            rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtv_desc.NumDescriptors = g_buffer_count;
            rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            g_device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&g_rtv_heap));
            g_rtv_descriptor_size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            
            // Create SRV Heap
            D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {};
            srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srv_desc.NumDescriptors = kMaxSrvDescriptors;
            srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            g_device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&g_srv_heap));
            g_srv_descriptor_size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            
            // Allocators & command list
            for (UINT i = 0; i < g_buffer_count; ++i) {
                g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frame_contexts[i].command_allocator));
            }
            g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frame_contexts[0].command_allocator, nullptr, IID_PPV_ARGS(&g_command_list));
            g_command_list->Close();

            CreateRenderTargets(swapchain);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            OverrideImGuiClipboardFunctions();
            InitializeOverlay();

            ImGui_ImplWin32_Init(hwnd);
            
            D3D12_CPU_DESCRIPTOR_HANDLE font_cpu = g_srv_heap->GetCPUDescriptorHandleForHeapStart();
            D3D12_GPU_DESCRIPTOR_HANDLE font_gpu = g_srv_heap->GetGPUDescriptorHandleForHeapStart();
            
            ImGui_ImplDX12_Init(g_device, g_buffer_count, DXGI_FORMAT_R8G8B8A8_UNORM, g_srv_heap, font_cpu, font_gpu);

            WNDPROC old_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));
            GetOverlayState().original_wnd_proc.store(old_proc, std::memory_order_release);

            GetOverlayState().active_dx_version.store("DirectX 12", std::memory_order_release);
            g_imgui_initialized = true;
            dover::shared::LogInfo("Dear ImGui initialized inside D3D12 Present hook. Device: %p", g_device);
            dover::shared::LogDebug("DX12 Initialization parameters -> HWND: %p, Buffers: %u", hwnd, g_buffer_count);
        }
    }

    if (g_imgui_initialized.load() && g_command_queue) {
        ProcessDeferredUploads();
        
        // Early-out: only run ImGui lifecycle if overlay is visible or OSD features are enabled
        bool should_render = GetOverlayState().show_overlay || 
                            shared::GetAppConfig().show_fps || 
                            shared::GetAppConfig().show_clock || 
                            shared::GetAppConfig().show_api;

        if (should_render) {
            IDXGISwapChain3* swapchain3 = nullptr;
            if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3)))) {
                UINT backbuffer_index = swapchain3->GetCurrentBackBufferIndex();
                swapchain3->Release();
                
                FrameContext& frame = g_frame_contexts[backbuffer_index];
                
                // Wait for GPU if allocator is still in flight
                if (g_fence && frame.fence_value != 0 && g_fence->GetCompletedValue() < frame.fence_value) {
                    g_fence->SetEventOnCompletion(frame.fence_value, g_fence_event);
                    WaitForSingleObject(g_fence_event, INFINITE);
                }
                
                if (frame.command_allocator) {
                    frame.command_allocator->Reset();
                    g_command_list->Reset(frame.command_allocator, nullptr);

                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource = frame.backbuffer;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    g_command_list->ResourceBarrier(1, &barrier);

                    g_command_list->OMSetRenderTargets(1, &frame.rtv_handle, FALSE, nullptr);
                    g_command_list->SetDescriptorHeaps(1, &g_srv_heap);

                    GetOverlayState().in_overlay_frame = true;
                    ImGui_ImplDX12_NewFrame();

                    shared::g_allow_xinput = true;
                    shared::g_allow_input_queries = true;
                    g_in_imgui_new_frame = true;
                    ImGui_ImplWin32_NewFrame();
                    g_in_imgui_new_frame = false;

                    
                    // Fix for blurry UI & cursor mismatch when game does not resize swapchain
                    ImGuiIO& io = ImGui::GetIO();
                    uint32_t swap_w = GetOverlayState().swapchain_width.load(std::memory_order_relaxed);
                    uint32_t swap_h = GetOverlayState().swapchain_height.load(std::memory_order_relaxed);
                    if (swap_w > 0 && swap_h > 0) {
                        io.DisplaySize = ImVec2(static_cast<float>(swap_w), static_cast<float>(swap_h));
                    }

                    ImGui::NewFrame();
                    RenderImGuiUI();
                    ImGui::Render();
                    
                    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_command_list);
                    
                    shared::g_allow_input_queries = false;
                    shared::g_allow_xinput = false;
                    GetOverlayState().in_overlay_frame.store(false, std::memory_order_relaxed);

                    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                    g_command_list->ResourceBarrier(1, &barrier);

                    g_command_list->Close();
                    ID3D12CommandList* ppCommandLists[] = { g_command_list };
                    g_command_queue->ExecuteCommandLists(1, ppCommandLists);
                    
                    g_fence_value++;
                    g_command_queue->Signal(g_fence, g_fence_value);
                    frame.fence_value = g_fence_value;
                }
            }
        }
    }

    HRESULT hr = g_original_present ? g_original_present(swapchain, sync_interval, flags) : S_OK;
    
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        dover::shared::LogError("DX12 Device Lost detected (HR: 0x%08X). Resetting overlay state.", hr);
        ResetDx12State(true);
    }
    
    return hr;
}

HRESULT WINAPI HookedPresent(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags) {
    __try {
        return HookedPresentInternal(swapchain, sync_interval, flags);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dover::shared::LogError("DX12 FATAL: HookedPresent Access Violation. SwapChain: %p", swapchain);
        return g_original_present ? g_original_present(swapchain, sync_interval, flags) : S_OK;
    }
}

HRESULT WINAPI HookedResizeBuffersInternal(IDXGISwapChain* swapchain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    if (g_imgui_initialized.load()) {
        CleanupRenderTargets();
        ImGui_ImplDX12_InvalidateDeviceObjects();
    }

    HRESULT hr = g_original_resize_buffers ? g_original_resize_buffers(swapchain, buffer_count, width, height, format, flags) : S_OK;

    if (SUCCEEDED(hr) && g_imgui_initialized.load()) {
        DXGI_SWAP_CHAIN_DESC desc = {};
        swapchain->GetDesc(&desc);
        g_buffer_count = desc.BufferCount;
        GetOverlayState().swapchain_width.store(desc.BufferDesc.Width, std::memory_order_relaxed);
        GetOverlayState().swapchain_height.store(desc.BufferDesc.Height, std::memory_order_relaxed);
        if (g_buffer_count > kMaxFrameBuffers) g_buffer_count = kMaxFrameBuffers;
        
        ImGui_ImplDX12_CreateDeviceObjects();
        CreateRenderTargets(swapchain);
    }

    return hr;
}

HRESULT WINAPI HookedResizeBuffers(IDXGISwapChain* swapchain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    __try {
        return HookedResizeBuffersInternal(swapchain, buffer_count, width, height, format, flags);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dover::shared::LogError("DX12 FATAL: HookedResizeBuffers Access Violation.");
        return g_original_resize_buffers ? g_original_resize_buffers(swapchain, buffer_count, width, height, format, flags) : S_OK;
    }
}

} // namespace

bool InitializeDx12Hook() {
    if (!InitializeHookSystem()) {
        dover::shared::LogError("MinHook initialization failed for DX12.");
        return false;
    }

    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    if (!d3d12 || !dxgi) return false;

    auto pD3D12CreateDevice = reinterpret_cast<D3D12CreateDeviceFn>(GetProcAddress(d3d12, "D3D12CreateDevice"));
    auto pCreateDXGIFactory1 = reinterpret_cast<CreateDXGIFactory1Fn>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    if (!pD3D12CreateDevice || !pCreateDXGIFactory1) return false;

    ID3D12Device* dummy_device = nullptr;
    if (FAILED(pD3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dummy_device)))) return false;

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    
    ID3D12CommandQueue* dummy_queue = nullptr;
    if (FAILED(dummy_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&dummy_queue)))) {
        dummy_device->Release();
        return false;
    }

    IDXGIFactory4* factory = nullptr;
    if (FAILED(pCreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        dummy_queue->Release();
        dummy_device->Release();
        return false;
    }

    HWND dummy_window = CreateWindowExA(0, "STATIC", "dummy", 0, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    if (!dummy_window) {
        factory->Release();
        dummy_queue->Release();
        dummy_device->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferDesc.Width = 1;
    desc.BufferDesc.Height = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = dummy_window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain* dummy_swapchain = nullptr;
    if (FAILED(factory->CreateSwapChain(dummy_queue, &desc, &dummy_swapchain))) {
        DestroyWindow(dummy_window);
        factory->Release();
        dummy_queue->Release();
        dummy_device->Release();
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(dummy_swapchain);
    void** queue_vtable = *reinterpret_cast<void***>(dummy_queue);

    if (!g_execute_hooked.load()) {
        if (CreateAndEnableHook(queue_vtable[kExecuteCommandListsIndex], reinterpret_cast<void*>(&HookedExecuteCommandLists),
                                reinterpret_cast<void**>(&g_original_execute_command_lists))) {
            g_execute_hooked = true;
        }
    }

    if (!g_present_hooked.load()) {
        if (CreateAndEnableHook(vtable[kPresentIndex], reinterpret_cast<void*>(&HookedPresent),
                                reinterpret_cast<void**>(&g_original_present))) {
            g_present_hooked = true;
        }
    }

    if (!g_resize_hooked.load()) {
        if (CreateAndEnableHook(vtable[kResizeBuffersIndex], reinterpret_cast<void*>(&HookedResizeBuffers),
                                reinterpret_cast<void**>(&g_original_resize_buffers))) {
            g_resize_hooked = true;
        }
    }

    dummy_swapchain->Release();
    DestroyWindow(dummy_window);
    factory->Release();
    dummy_queue->Release();
    dummy_device->Release();

    if (g_present_hooked.load()) {
        dover::shared::LogInfo("DX12 Hook installed via dummy swapchain.");
        return true;
    }

    dover::shared::LogError("Failed to hook DX12 functions.");
    return false;
}

void ResetDx12State(bool device_lost) {
    if (g_imgui_initialized.load()) {
        if (!device_lost) {
            WaitForGpu();
        }
        
        HWND hwnd = GetOverlayState().game_hwnd.load(std::memory_order_acquire);
        WNDPROC orig = GetOverlayState().original_wnd_proc.load(std::memory_order_acquire);
        if (hwnd && orig) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
        }
        
        // When device is lost, releasing COM objects is safe, but we skip waiting for GPU and cleanup logic that assumes a live device.
        CleanupRenderTargets();
        
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        
        for (UINT i = 0; i < g_allocated_textures_count; ++i) {
            if (g_allocated_textures[i].resource) {
                g_allocated_textures[i].resource->Release();
                g_allocated_textures[i].resource = nullptr;
            }
        }
        g_allocated_textures_count = 0;
        
        for (UINT i = 0; i < g_deferred_uploads_count; ++i) {
            if (g_deferred_uploads[i].upload_buffer) {
                g_deferred_uploads[i].upload_buffer->Release();
            }
        }
        g_deferred_uploads_count = 0;

        for (UINT i = 0; i < kMaxFrameBuffers; ++i) {
            if (g_frame_contexts[i].command_allocator) {
                g_frame_contexts[i].command_allocator->Release();
                g_frame_contexts[i].command_allocator = nullptr;
            }
        }
        
        if (g_command_list) { g_command_list->Release(); g_command_list = nullptr; }
        if (g_rtv_heap) { g_rtv_heap->Release(); g_rtv_heap = nullptr; }
        if (g_srv_heap) { g_srv_heap->Release(); g_srv_heap = nullptr; }
        
        if (g_fence) { g_fence->Release(); g_fence = nullptr; }
        if (g_fence_event) { CloseHandle(g_fence_event); g_fence_event = nullptr; }
        
        if (g_command_queue) { g_command_queue->Release(); g_command_queue = nullptr; }
        if (g_device) { g_device->Release(); g_device = nullptr; }
        
        shared::SetDx12Device(nullptr);
        shared::g_CreateDx12TextureFn = nullptr;
        shared::g_ReleaseDx12TextureFn = nullptr;
        g_imgui_initialized = false;
    }
}

void ShutdownDx12Hook() {
    HWND hwnd = GetOverlayState().game_hwnd.load(std::memory_order_acquire);
    WNDPROC orig = GetOverlayState().original_wnd_proc.load(std::memory_order_acquire);
    if (hwnd && orig) {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
    }
    ResetDx12State(false);

    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_execute_command_lists));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_present));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_resize_buffers));

    g_original_execute_command_lists = nullptr;
    g_original_present = nullptr;
    g_original_resize_buffers = nullptr;
    
    g_execute_hooked = false;
    g_present_hooked = false;
    g_resize_hooked = false;
}

void* CreateDx12TextureInternal(const uint8_t* rgba_data, int width, int height) {
    if (!g_device || !g_command_queue || !g_command_list || !g_srv_heap) {
        dover::shared::LogError("CreateDx12TextureInternal: Null pointer present (device: %p, queue: %p, list: %p, heap: %p)", g_device, g_command_queue, g_command_list, g_srv_heap);
        return nullptr;
    }

    dover::shared::LogInfo("CreateDx12TextureInternal: loading w=%d, h=%d, rgba_data=%p", width, height, rgba_data);

    // Find free SRV index (0 is ImGui Font)
    int srv_index = -1;
    bool used_slots[kMaxSrvDescriptors] = {false};
    used_slots[0] = true;
    for (UINT i = 0; i < g_allocated_textures_count; ++i) {
        const auto& entry = g_allocated_textures[i];
        if (entry.srv_index >= 0 && entry.srv_index < kMaxSrvDescriptors) {
            used_slots[entry.srv_index] = true;
        }
    }
    for (int i = 1; i < kMaxSrvDescriptors; ++i) {
        if (!used_slots[i]) {
            srv_index = i;
            break;
        }
    }
    
    if (srv_index == -1) {
        dover::shared::LogError("CreateDx12TextureInternal: No free SRV descriptors available.");
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES default_heap = {};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* tex_resource = nullptr;
    HRESULT hr = g_device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex_resource));
    if (FAILED(hr)) {
        dover::shared::LogError("CreateDx12TextureInternal: CreateCommittedResource default_heap failed (HR: 0x%08X)", hr);
        return nullptr;
    }

    D3D12_HEAP_PROPERTIES upload_heap = {};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    
    UINT64 upload_size = 0;
    g_device->GetCopyableFootprints(&tex_desc, 0, 1, 0, nullptr, nullptr, nullptr, &upload_size);

    D3D12_RESOURCE_DESC upload_desc = {};
    upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_desc.Width = upload_size;
    upload_desc.Height = 1;
    upload_desc.DepthOrArraySize = 1;
    upload_desc.MipLevels = 1;
    upload_desc.Format = DXGI_FORMAT_UNKNOWN;
    upload_desc.SampleDesc.Count = 1;
    upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    upload_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* upload_buffer = nullptr;
    hr = g_device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload_buffer));
    if (FAILED(hr)) {
        dover::shared::LogError("CreateDx12TextureInternal: CreateCommittedResource upload_heap failed (HR: 0x%08X)", hr);
        tex_resource->Release();
        return nullptr;
    }

    void* mapped_data = nullptr;
    D3D12_RANGE read_range = {0, 0};
    hr = upload_buffer->Map(0, &read_range, &mapped_data);
    if (SUCCEEDED(hr)) {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT num_rows;
        UINT64 row_size;
        UINT64 total_bytes;
        g_device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &num_rows, &row_size, &total_bytes);
        
        dover::shared::LogDebug("CreateDx12TextureInternal: mapping footprint offset=%llu, pitch=%u, total_bytes=%llu", 
            footprint.Offset, footprint.Footprint.RowPitch, total_bytes);

        uint8_t* dest = static_cast<uint8_t*>(mapped_data) + footprint.Offset;
        const uint8_t* src = rgba_data;
        if (!src) {
            dover::shared::LogError("CreateDx12TextureInternal: rgba_data is NULL!");
        } else {
            for (int y = 0; y < height; ++y) {
                memcpy(dest + y * footprint.Footprint.RowPitch, src + y * width * 4, width * 4);
            }
        }
        upload_buffer->Unmap(0, nullptr);
    } else {
        dover::shared::LogError("CreateDx12TextureInternal: upload_buffer->Map failed (HR: 0x%08X)", hr);
        upload_buffer->Release();
        tex_resource->Release();
        return nullptr;
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = tex_resource;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload_buffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    g_device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &src.PlacedFootprint, nullptr, nullptr, nullptr);

    g_command_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = tex_resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_command_list->ResourceBarrier(1, &barrier);

    if (g_deferred_uploads_count < kMaxSrvDescriptors) {
        g_deferred_uploads[g_deferred_uploads_count++] = {upload_buffer, g_fence_value + 1};
    } else {
        dover::shared::LogError("CreateDx12TextureInternal: g_deferred_uploads capacity exceeded!");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = g_srv_heap->GetCPUDescriptorHandleForHeapStart();
    cpu_handle.ptr += srv_index * g_srv_descriptor_size;
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    g_device->CreateShaderResourceView(tex_resource, &srv_desc, cpu_handle);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = g_srv_heap->GetGPUDescriptorHandleForHeapStart();
    gpu_handle.ptr += srv_index * g_srv_descriptor_size;
    void* tex_id = reinterpret_cast<void*>(gpu_handle.ptr);
    
    if (g_allocated_textures_count < kMaxSrvDescriptors) {
        g_allocated_textures[g_allocated_textures_count++] = {tex_id, tex_resource, srv_index};
    } else {
        dover::shared::LogError("CreateDx12TextureInternal: g_allocated_textures capacity exceeded!");
    }
    dover::shared::LogInfo("CreateDx12TextureInternal: Texture created. ID: %p, Resource: %p", tex_id, tex_resource);
    return tex_id;
}

void* CreateDx12Texture(const uint8_t* rgba_data, int width, int height) {
    __try {
        return CreateDx12TextureInternal(rgba_data, width, height);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dover::shared::LogError("CreateDx12Texture: CRASH intercepted. Exception code: 0x%08X", GetExceptionCode());
        return nullptr;
    }
}

void ReleaseDx12Texture(void* texture_id) {
    if (!texture_id) return;
    for (UINT i = 0; i < g_allocated_textures_count; ++i) {
        if (g_allocated_textures[i].tex_id == texture_id) {
            if (g_allocated_textures[i].resource) {
                // Must ensure GPU is done with this texture. 
                // Since this might be called randomly, we wait for GPU. 
                // Better: push to a deferred delete queue, but since this is rare (window reload), 
                // a GPU wait is acceptable, or we can just release it immediately if we're sure it's not bound.
                // For safety, we wait for GPU.
                WaitForGpu();
                g_allocated_textures[i].resource->Release();
            }
            g_allocated_textures[i] = g_allocated_textures[g_allocated_textures_count - 1];
            g_allocated_textures_count--;
            break;
        }
    }
}

} // namespace dover::overlay
