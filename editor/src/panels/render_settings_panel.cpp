#include "render_settings_panel.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/scene_settings.h"
#include "daedalus/editor/render_settings_data.h"

#include "imgui.h"

#include <algorithm>
#include <array>

namespace daedalus::editor
{

RenderSettingsPanel::RenderSettingsPanel(
    std::function<std::filesystem::path()> browseForFile)
    : m_browseForFile(std::move(browseForFile))
{}

// Helper: clamp a float value and return true if changed.
static inline bool DragClamped(const char* label, float* v,
                                float speed, float lo, float hi,
                                const char* fmt = "%.3f")
{
    const bool changed = ImGui::DragFloat(label, v, speed, lo, hi, fmt);
    *v = std::clamp(*v, lo, hi);
    return changed;
}

void RenderSettingsPanel::draw(EditMapDocument& doc)
{
    ImGui::Begin("Render Settings");

    SceneSettings&       ss  = doc.sceneSettings();
    RenderSettingsData&  rs  = doc.renderSettings();

    // ── Sun / Directional light ───────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Sun / Ambient", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SeparatorText("Sun");
        ImGui::ColorEdit3("Color##sun", &ss.sunColor.x);
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##sunint", &ss.sunIntensity, 0.05f, 0.0f, 20.0f, "Intensity: %.2f");

        ImGui::SeparatorText("Direction (normalised)");
        ImGui::DragFloat3("##sundir", &ss.sunDirection.x, 0.01f, -1.0f, 1.0f);

        ImGui::SeparatorText("Ambient");
        auto& mapData = doc.mapData();
        ImGui::ColorEdit3("Color##amb", &mapData.globalAmbientColor.x);
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##ambint", &mapData.globalAmbientIntensity, 0.01f, 0.0f, 10.0f, "Intensity: %.2f");
    }

    // ── Volumetric Fog ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Volumetric Fog"))
    {
        VolumetricFogData& fog = rs.fog;
        ImGui::Checkbox("Enabled##fog", &fog.enabled);
        ImGui::BeginDisabled(!fog.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##dens", &fog.density, 0.001f, 0.0f, 1.0f, "Density: %.4f");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##aniso", &fog.anisotropy, 0.01f, -1.0f, 1.0f, "Anisotropy: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##scat", &fog.scattering, 0.01f, 0.0f, 1.0f, "Scattering: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##fnear", &fog.fogNear, 0.1f, 0.0f, 500.0f, "Near: %.1f m");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##ffar", &fog.fogFar, 1.0f, 0.0f, 2000.0f, "Far: %.1f m");

        float fogAmb[3] = {fog.ambientFogR, fog.ambientFogG, fog.ambientFogB};
        if (ImGui::ColorEdit3("Ambient Fog##fog", fogAmb))
        {
            fog.ambientFogR = fogAmb[0];
            fog.ambientFogG = fogAmb[1];
            fog.ambientFogB = fogAmb[2];
        }
        ImGui::EndDisabled();
    }

    // ── Screen-Space Reflections ──────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Screen-Space Reflections (SSR)"))
    {
        SSRData& ssr = rs.ssr;
        ImGui::Checkbox("Enabled##ssr", &ssr.enabled);
        ImGui::BeginDisabled(!ssr.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##ssrmd", &ssr.maxDistance, 0.5f, 0.0f, 200.0f, "Max Dist: %.1f m");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##ssrth", &ssr.thickness, 0.005f, 0.0f, 1.0f, "Thickness: %.3f m");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##ssrrc", &ssr.roughnessCutoff, 0.01f, 0.0f, 1.0f, "Roughness Cutoff: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##ssrfs", &ssr.fadeStart, 0.005f, 0.0f, 0.5f, "Fade Start (UV): %.3f");

        int steps = static_cast<int>(ssr.maxSteps);
        if (ImGui::SliderInt("Max Steps##ssr", &steps, 8, 256))
            ssr.maxSteps = static_cast<uint32_t>(steps);

        ImGui::EndDisabled();
    }

    // ── Depth of Field ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Depth of Field"))
    {
        DoFData& dof = rs.dof;
        ImGui::Checkbox("Enabled##dof", &dof.enabled);
        ImGui::BeginDisabled(!dof.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##doffd", &dof.focusDistance, 0.1f, 0.1f, 500.0f, "Focus Dist: %.1f m");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##doffr", &dof.focusRange, 0.1f, 0.1f, 100.0f, "Focus Range: %.1f m");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##dofbr", &dof.bokehRadius, 0.5f, 0.0f, 32.0f, "Bokeh Radius: %.1f px");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##dofnt", &dof.nearTransition, 0.05f, 0.0f, 20.0f, "Near Trans: %.2f m");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##dofft", &dof.farTransition, 0.05f, 0.0f, 50.0f, "Far Trans: %.2f m");

        ImGui::EndDisabled();
    }

    // ── Motion Blur ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Motion Blur"))
    {
        MotionBlurData& mb = rs.motionBlur;
        ImGui::Checkbox("Enabled##mb", &mb.enabled);
        ImGui::BeginDisabled(!mb.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##mbsa", &mb.shutterAngle, 0.01f, 0.0f, 1.0f, "Shutter Angle: %.2f");

        int samp = static_cast<int>(mb.numSamples);
        if (ImGui::SliderInt("Samples##mb", &samp, 2, 32))
            mb.numSamples = static_cast<uint32_t>(samp);

        ImGui::EndDisabled();
    }

    // ── Colour Grading ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Colour Grading"))
    {
        ColorGradingData& cg = rs.colorGrading;
        ImGui::Checkbox("Enabled##cg", &cg.enabled);
        ImGui::BeginDisabled(!cg.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##cgint", &cg.intensity, 0.01f, 0.0f, 1.0f, "Intensity: %.2f");
        // LUT path: text field + optional browse button.
        {
            std::array<char, 512> buf{};
            const std::string& cur = cg.lutPath;
            const std::size_t  cpLen = std::min(cur.size(), buf.size() - 1u);
            std::copy_n(cur.c_str(), cpLen, buf.data());

            ImGui::SetNextItemWidth(m_browseForFile ? -70.0f : -1.0f);
            if (ImGui::InputText("##lut", buf.data(), buf.size()))
                cg.lutPath = buf.data();

            if (m_browseForFile)
            {
                ImGui::SameLine();
                if (ImGui::Button("Browse##lut"))
                {
                    const auto chosen = m_browseForFile();
                    if (!chosen.empty())
                        cg.lutPath = chosen.string();
                }
            }
            ImGui::TextDisabled("1024x32 RGBA8 PNG strip (32x32x32 LUT)");
        }

        ImGui::EndDisabled();
    }

    // ── Optional FX ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Optional FX"))
    {
        OptionalFxData& fx = rs.optionalFx;
        ImGui::Checkbox("Enabled##fx", &fx.enabled);
        ImGui::BeginDisabled(!fx.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##fxca", &fx.caAmount, 0.001f, 0.0f, 0.05f, "Chromatic Ab: %.4f");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##fxvi", &fx.vignetteIntensity, 0.01f, 0.0f, 1.0f, "Vignette Str: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##fxvr", &fx.vignetteRadius, 0.01f, 0.0f, 1.0f, "Vignette Rad: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        DragClamped("##fxgr", &fx.grainAmount, 0.001f, 0.0f, 0.2f, "Film Grain: %.4f");

        ImGui::EndDisabled();
    }

    // ── Upscaling / Anti-aliasing ─────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Upscaling / AA"))
    {
        ImGui::Checkbox("FXAA", &rs.upscaling.fxaaEnabled);
    }

    ImGui::End();
}

} // namespace daedalus::editor
