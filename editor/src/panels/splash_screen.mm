// splash_screen.mm
// Cinematic startup splash modal for DaedalusEdit.
//
// Rendered as an ImGui modal popup:
//   ┌──────────────────────────────────────────────┐
//   │  16:9 image box (black-barred if 3:2 source) │  450 px
//   ├──────────────────────────────────────────────┤
//   │          DaedalusEdit                        │
//   │       Created by : Jason Blundell            │
//   │  macOS X.Y  ·  Apple M3 Max  ·  64 GB RAM   │
//   │  "Assembling artisanal triangles by hand…"   │
//   └──────────────────────────────────────────────┘

#import <Metal/Metal.h>
#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSString.h>

#include "splash_screen.h"
#include "imgui.h"

// stb_image declarations only — implementation lives in DaedalusRender.
#include "stb_image.h"

#include <sys/sysctl.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace daedalus::editor
{

// ─── Message table ────────────────────────────────────────────────────────────

static const char* const k_msgs[] = {
    "Consulting Daedalus\xe2\x80\xa6 please stand clear of unfinished genius.",
    "Waxing the wings of the renderer\xe2\x80\xa6",
    "Checking labyrinth integrity\xe2\x80\xa6 no minotaurs detected.",
    "Sharpening the drafting stylus\xe2\x80\xa6",
    "Re-threading the thread of invention\xe2\x80\xa6",
    "Calibrating impossible architecture\xe2\x80\xa6",
    "Folding geometry into something only mostly dangerous\xe2\x80\xa6",
    "Testing wing physics below recommended sun levels\xe2\x80\xa6",
    "Laying out corridors with suspicious confidence\xe2\x80\xa6",
    "Assembling artisanal triangles by hand\xe2\x80\xa6",
    "Applying myth-grade structural overengineering\xe2\x80\xa6",
    "Measuring twice, escaping Crete once\xe2\x80\xa6",
    "Drafting cubes in dramatic profile\xe2\x80\xa6",
    "Turning parchment notes into load-bearing reality\xe2\x80\xa6",
    "Checking the workshop for feathers, wax, and race conditions\xe2\x80\xa6",
    "Routing pathways through the impossible\xe2\x80\xa6",
    "Politely ignoring the laws of ordinary masonry\xe2\x80\xa6",
    "Daedalus is muttering about angles again\xe2\x80\xa6",
    "Tuning the engine until it hums like divine inspiration\xe2\x80\xa6",
    "Mapping corridors no sane architect would approve\xe2\x80\xa6",
    "Reinforcing joints against hubris\xe2\x80\xa6",
    "Loading handcrafted brilliance\xe2\x80\xa6 fingerprints included.",
    "Consulting ancient Greek debug methodology\xe2\x80\xa6",
    "Preparing an elegant solution to an elaborate problem\xe2\x80\xa6",
    "Warming up the workshop furnace of invention\xe2\x80\xa6",
    "Engine readying for flight. Avoid the sun.",
};

static constexpr int   k_msgCount  = static_cast<int>(sizeof(k_msgs) / sizeof(k_msgs[0]));
static constexpr float k_msgDur    = 2.2f;   ///< Seconds per message.
static constexpr float k_fadeDur   = 0.4f;   ///< Fade-in / fade-out duration.
static constexpr float k_dispTime  = 4.5f;   ///< Auto-close after this many seconds.
static constexpr float k_clickMin  = 1.0f;   ///< Earliest a click/key can dismiss.

// ─── Layout constants ─────────────────────────────────────────────────────────

static constexpr float k_modalW    = 800.0f;
static constexpr float k_imgAreaH  = 450.0f;  ///< 16:9 at 800 px wide.
static constexpr float k_txtAreaH  = 130.0f;
static constexpr float k_modalH    = k_imgAreaH + k_txtAreaH;

// ─── SplashScreen::~SplashScreen ─────────────────────────────────────────────

SplashScreen::~SplashScreen()
{
    if (m_texture)
    {
        @autoreleasepool
        {
            // Transfer ownership back to ARC — the texture is then released.
            id<MTLTexture> released = (__bridge_transfer id<MTLTexture>)(void*)m_texture;
            (void)released;
        }
        m_texture = nullptr;
    }
}

// ─── SplashScreen::init ───────────────────────────────────────────────────────

void SplashScreen::init(void* mtlDevice, const std::string& resDir)
{
    // ── Load splash image ─────────────────────────────────────────────────────
    {
        const std::string path = resDir + "/editor_assets/splash.png";
        int channels = 0;
        stbi_uc* px  = stbi_load(path.c_str(), &m_texW, &m_texH, &channels, 4);

        if (px && mtlDevice)
        {
            @autoreleasepool
            {
                id<MTLDevice> dev = (__bridge id<MTLDevice>)mtlDevice;

                MTLTextureDescriptor* desc =
                    [MTLTextureDescriptor
                        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                     width:(NSUInteger)m_texW
                                                    height:(NSUInteger)m_texH
                                                 mipmapped:NO];
                desc.usage       = MTLTextureUsageShaderRead;
                desc.storageMode = MTLStorageModeShared;

                id<MTLTexture> tex = [dev newTextureWithDescriptor:desc];
                [tex replaceRegion:MTLRegionMake2D(0, 0, m_texW, m_texH)
                       mipmapLevel:0
                         withBytes:px
                       bytesPerRow:(NSUInteger)m_texW * 4];

                // Explicitly retain so ARC doesn't release it on scope exit.
                m_texture = (__bridge_retained void*)tex;
            }
            stbi_image_free(px);
        }
    }

    // ── Gather hardware / OS info ─────────────────────────────────────────────
    @autoreleasepool
    {
        // GPU name
        std::string gpuName;
        if (mtlDevice)
        {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)mtlDevice;
            if (dev.name)
                gpuName = [dev.name UTF8String];
        }

        // CPU brand string (Intel) or chip name via Metal device (Apple Silicon).
        std::string cpuName;
        {
            char brand[256] = {};
            size_t len = sizeof(brand);
            sysctlbyname("machdep.cpu.brand_string", brand, &len, nullptr, 0);
            cpuName = brand[0] ? std::string(brand) : gpuName;
        }

        // Physical + logical core counts.
        std::string coreStr;
        {
            int32_t phys = 0, logi = 0;
            size_t  sz   = sizeof(phys);
            sysctlbyname("hw.physicalcpu", &phys, &sz, nullptr, 0);
            sz = sizeof(logi);
            sysctlbyname("hw.logicalcpu",  &logi, &sz, nullptr, 0);

            if (phys > 0 && logi > 0 && phys != logi)
                coreStr = std::format("{} cores ({} logical)", phys, logi);
            else if (phys > 0)
                coreStr = std::format("{} cores", phys);
        }

        // Total RAM in GB.
        std::string ramStr;
        {
            uint64_t memBytes = 0;
            size_t   sz       = sizeof(memBytes);
            sysctlbyname("hw.memsize", &memBytes, &sz, nullptr, 0);
            ramStr = std::format("{} GB RAM", memBytes / (1024ULL * 1024ULL * 1024ULL));
        }

        // macOS version string.
        std::string osStr;
        {
            NSString* ver = NSProcessInfo.processInfo.operatingSystemVersionString;
            if (ver)
                osStr = [ver UTF8String];
            // Strip "Version " prefix emitted by some macOS releases.
            if (osStr.starts_with("Version "))
                osStr = osStr.substr(8);
        }

        // Build the one-line info string: "macOS 14.5  ·  Apple M3 Max  ·  12 cores  ·  64 GB RAM"
        // The UTF-8 sequence \xc2\xb7 is the middle-dot (·) separator.
        auto append = [&](const std::string& s)
        {
            if (s.empty()) return;
            if (!m_infoLine.empty()) m_infoLine += "   \xc2\xb7   ";
            m_infoLine += s;
        };
        append(osStr);
        append(cpuName);
        append(coreStr);
        append(ramStr);
        // Only show GPU separately if it differs from the CPU string (non-Apple Silicon).
        if (!gpuName.empty() && gpuName != cpuName)
            append("GPU: " + gpuName);
    }
}

// ─── SplashScreen::draw ───────────────────────────────────────────────────────

void SplashScreen::draw(float dt)
{
    if (!m_visible) return;

    m_total += dt;
    m_msgT  += dt;
    if (m_msgT >= k_msgDur)
    {
        m_msgT  -= k_msgDur;
        m_msgIdx = (m_msgIdx + 1) % k_msgCount;
    }

    // Open the popup exactly once on the first draw call.
    if (!m_opened)
    {
        ImGui::OpenPopup("##Splash");
        m_opened = true;
    }

    // Center on the main viewport.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({k_modalW, k_modalH}, ImGuiCond_Always);

    // ── Style overrides ───────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_PopupBg,          {0.05f, 0.04f, 0.06f, 1.00f});
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, {0.00f, 0.00f, 0.00f, 0.90f});
    ImGui::PushStyleColor(ImGuiCol_Border,            {0.42f, 0.35f, 0.18f, 0.90f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoNav             |
        ImGuiWindowFlags_NoMove;

    bool isOpen = true;
    if (ImGui::BeginPopupModal("##Splash", &isOpen, kFlags))
    {
        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        const ImVec2 wPos = ImGui::GetWindowPos();

        // ── 16:9 image area ───────────────────────────────────────────────────
        // Fill the entire box black first (produces black bars for non-16:9 images).
        dl->AddRectFilled(wPos,
                          {wPos.x + k_modalW, wPos.y + k_imgAreaH},
                          IM_COL32(0, 0, 0, 255));

        if (m_texture)
        {
            // Compute letter/pillarbox rects to "contain" image in 16:9 box.
            const float aspect = float(m_texW) / float(m_texH);
            const float boxAsp = k_modalW / k_imgAreaH;
            float dw, dh, ox = 0.0f, oy = 0.0f;

            if (aspect < boxAsp)
            {
                // Image is narrower than 16:9 → fit height, black bars left/right.
                dh = k_imgAreaH;
                dw = dh * aspect;
                ox = (k_modalW - dw) * 0.5f;
            }
            else
            {
                // Image is wider than 16:9 → fit width, black bars top/bottom.
                dw = k_modalW;
                dh = dw / aspect;
                oy = (k_imgAreaH - dh) * 0.5f;
            }

            dl->AddImage(
                (ImTextureID)(void*)m_texture,
                {wPos.x + ox,      wPos.y + oy},
                {wPos.x + ox + dw, wPos.y + oy + dh});
        }

        // Cinematic letterbox bars — drawn over the image so they are always
        // solid black regardless of the image's native aspect ratio.
        constexpr float kBarH = 32.0f;
        dl->AddRectFilled(wPos,
                          {wPos.x + k_modalW, wPos.y + kBarH},
                          IM_COL32(0, 0, 0, 255));
        dl->AddRectFilled({wPos.x, wPos.y + k_imgAreaH - kBarH},
                          {wPos.x + k_modalW, wPos.y + k_imgAreaH},
                          IM_COL32(0, 0, 0, 255));

        // Gradient vignette — blends the bottom bar into the text strip below.
        dl->AddRectFilledMultiColor(
            {wPos.x, wPos.y + k_imgAreaH - 100.0f},
            {wPos.x + k_modalW, wPos.y + k_imgAreaH - kBarH},
            IM_COL32(0, 0, 0,   0), IM_COL32(0, 0, 0,   0),
            IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 200));

        // Advance the ImGui cursor past the image area.
        ImGui::SetCursorPos({0.0f, k_imgAreaH});

        // ── Text area helpers ─────────────────────────────────────────────────
        const float winW = ImGui::GetWindowWidth();

        // Render text centered horizontally at the given font scale and colour.
        auto ctext = [&](const char* s, float scale, ImVec4 col)
        {
            ImGui::SetWindowFontScale(scale);
            const float tw = ImGui::CalcTextSize(s).x;
            ImGui::SetCursorPosX(std::max(6.0f, (winW - tw) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(s);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f);
        };

        // ── Title ─────────────────────────────────────────────────────────────
        ImGui::SetCursorPosY(k_imgAreaH + 10.0f);
        ctext("DaedalusEdit", 2.0f, {0.95f, 0.82f, 0.45f, 1.0f});

        // ── Author ────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ctext("Created by : Jason Blundell", 1.15f, {0.80f, 0.75f, 0.65f, 1.0f});

        // ── Separator ─────────────────────────────────────────────────────────
        ImGui::Spacing();
        {
            const float ly  = wPos.y + ImGui::GetCursorPosY();
            const float lx0 = wPos.x + 48.0f;
            const float lx1 = wPos.x + k_modalW - 48.0f;
            dl->AddLine({lx0, ly}, {lx1, ly}, IM_COL32(100, 84, 42, 150), 1.0f);
            ImGui::Dummy({0.0f, 3.0f});
        }

        // ── Machine / OS info ─────────────────────────────────────────────────
        if (!m_infoLine.empty())
        {
            ImGui::Spacing();
            ctext(m_infoLine.c_str(), 0.82f, {0.52f, 0.50f, 0.46f, 1.0f});
        }

        // ── Cycling message with fade-in / fade-out ───────────────────────────
        {
            const float t = m_msgT;
            float alpha   = 1.0f;
            if (t < k_fadeDur)
                alpha = t / k_fadeDur;
            else if (t > k_msgDur - k_fadeDur)
                alpha = std::max(0.0f, (k_msgDur - t) / k_fadeDur);

            ImGui::Spacing();
            ctext(k_msgs[m_msgIdx], 1.0f, {0.88f, 0.78f, 0.52f, alpha});
        }

        // ── "Click to continue" hint (pulsing, appears after click delay) ─────
        if (m_total > k_clickMin)
        {
            const float pa = 0.28f + 0.22f * std::sin(m_total * 2.6f);
            ImGui::Spacing();
            ctext("Click to continue", 0.82f, {0.45f, 0.42f, 0.36f, pa});
        }

        ImGui::SetWindowFontScale(1.0f);  // safety reset

        // ── Dismiss ───────────────────────────────────────────────────────────
        const bool autoClose = (m_total >= k_dispTime);
        const bool clickClose = (m_total > k_clickMin)
            && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool keyClose = (m_total > k_clickMin)
            && (ImGui::IsKeyPressed(ImGuiKey_Escape)
             || ImGui::IsKeyPressed(ImGuiKey_Space)
             || ImGui::IsKeyPressed(ImGuiKey_Enter));

        if (autoClose || clickClose || keyClose || !isOpen)
        {
            m_visible = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

} // namespace daedalus::editor
