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
    std::function<void()> onBrowseRequested)
    : m_onBrowseRequested(std::move(onBrowseRequested))
{}

void RenderSettingsPanel::deliverPath(const std::filesystem::path& path)
{
    if (!path.empty())
    {
        m_pendingLutPath = path;
        m_hasLutPath     = true;
    }
}

void RenderSettingsPanel::draw(EditMapDocument& doc)
{
    // Apply any LUT path delivered from the async file dialog.
    if (m_hasLutPath)
    {
        doc.renderSettings().colorGrading.lutPath = m_pendingLutPath.string();
        m_pendingLutPath.clear();
        m_hasLutPath = false;
    }

    ImGui::Begin("Render Settings");

    SceneSettings&       ss  = doc.sceneSettings();
    RenderSettingsData&  rs  = doc.renderSettings();

    // ── Sun / Directional light ───────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Sun / Ambient", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SeparatorText("Sun");
        ImGui::ColorEdit3("Color##sun", &ss.sunColor.x);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##sunint", &ss.sunIntensity, 0.05f, 0.0f, 0.0f, "Intensity: %.2f");

        ImGui::SeparatorText("Direction (normalised)");
        ImGui::DragFloat3("##sundir", &ss.sunDirection.x, 0.01f, -1.0f, 1.0f);

        ImGui::SeparatorText("Ambient");
        auto& mapData = doc.mapData();
        ImGui::ColorEdit3("Color##amb", &mapData.globalAmbientColor.x);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##ambint", &mapData.globalAmbientIntensity, 0.01f, 0.0f, 0.0f, "Intensity: %.2f");
    }

    // ── Volumetric Fog ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Volumetric Fog"))
    {
        VolumetricFogData& fog = rs.fog;
        ImGui::Checkbox("Enabled##fog", &fog.enabled);
        ImGui::BeginDisabled(!fog.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##dens", &fog.density, 0.001f, 0.0f, 0.0f, "Density: %.4f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##aniso", &fog.anisotropy, 0.01f, 0.0f, 0.0f, "Anisotropy: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##scat", &fog.scattering, 0.01f, 0.0f, 0.0f, "Scattering: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##fnear", &fog.fogNear, 0.1f, 0.0f, 0.0f, "Near: %.1f m");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##ffar", &fog.fogFar, 1.0f, 0.0f, 0.0f, "Far: %.1f m");

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
        ImGui::DragFloat("##ssrmd", &ssr.maxDistance, 0.5f, 0.0f, 0.0f, "Max Dist: %.1f m");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##ssrth", &ssr.thickness, 0.005f, 0.0f, 0.0f, "Thickness: %.3f m");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##ssrrc", &ssr.roughnessCutoff, 0.01f, 0.0f, 0.0f, "Roughness Cutoff: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##ssrfs", &ssr.fadeStart, 0.005f, 0.0f, 0.0f, "Fade Start (UV): %.3f");

        int steps = static_cast<int>(ssr.maxSteps);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragInt("##ssrmsteps", &steps, 1.0f, 0, 0, "Max Steps: %d"))
            ssr.maxSteps = static_cast<uint32_t>(std::max(1, steps));

        ImGui::EndDisabled();
    }

    // ── Depth of Field ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Depth of Field"))
    {
        DoFData& dof = rs.dof;
        ImGui::Checkbox("Enabled##dof", &dof.enabled);
        ImGui::BeginDisabled(!dof.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##doffd", &dof.focusDistance, 0.1f, 0.0f, 0.0f, "Focus Dist: %.1f m");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##doffr", &dof.focusRange, 0.1f, 0.0f, 0.0f, "Focus Range: %.1f m");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##dofbr", &dof.bokehRadius, 0.5f, 0.0f, 0.0f, "Bokeh Radius: %.1f px");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##dofnt", &dof.nearTransition, 0.05f, 0.0f, 0.0f, "Near Trans: %.2f m");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##dofft", &dof.farTransition, 0.05f, 0.0f, 0.0f, "Far Trans: %.2f m");

        ImGui::EndDisabled();
    }

    // ── Motion Blur ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Motion Blur"))
    {
        MotionBlurData& mb = rs.motionBlur;
        ImGui::Checkbox("Enabled##mb", &mb.enabled);
        ImGui::BeginDisabled(!mb.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##mbsa", &mb.shutterAngle, 0.01f, 0.0f, 0.0f, "Shutter Angle: %.2f");

        int samp = static_cast<int>(mb.numSamples);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragInt("##mbsamp", &samp, 1.0f, 0, 0, "Samples: %d"))
            mb.numSamples = static_cast<uint32_t>(std::max(1, samp));

        ImGui::EndDisabled();
    }

    // ── Colour Grading ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Colour Grading"))
    {
        ColorGradingData& cg = rs.colorGrading;
        ImGui::Checkbox("Enabled##cg", &cg.enabled);
        ImGui::BeginDisabled(!cg.enabled);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##cgint", &cg.intensity, 0.01f, 0.0f, 0.0f, "Intensity: %.2f");
        // LUT path: text field + optional browse button.
        {
            std::array<char, 512> buf{};
            const std::string& cur = cg.lutPath;
            const std::size_t  cpLen = std::min(cur.size(), buf.size() - 1u);
            std::copy_n(cur.c_str(), cpLen, buf.data());

            ImGui::SetNextItemWidth(m_onBrowseRequested ? -70.0f : -1.0f);
            if (ImGui::InputText("##lut", buf.data(), buf.size()))
                cg.lutPath = buf.data();

            if (m_onBrowseRequested)
            {
                ImGui::SameLine();
                if (ImGui::Button("Browse##lut"))
                    m_onBrowseRequested();  // async — result delivered via deliverPath()
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
        ImGui::DragFloat("##fxca", &fx.caAmount, 0.001f, 0.0f, 0.0f, "Chromatic Ab: %.4f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##fxvi", &fx.vignetteIntensity, 0.01f, 0.0f, 0.0f, "Vignette Str: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##fxvr", &fx.vignetteRadius, 0.01f, 0.0f, 0.0f, "Vignette Rad: %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat("##fxgr", &fx.grainAmount, 0.001f, 0.0f, 0.0f, "Film Grain: %.4f");

        ImGui::EndDisabled();
    }

    // ── Upscaling / Anti-aliasing ─────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Upscaling / AA"))
    {
        ImGui::Checkbox("FXAA", &rs.upscaling.fxaaEnabled);
    }

    // ── Ray Tracing ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Ray Tracing"))
    {
        RTData& rt = rs.rt;
        ImGui::Checkbox("Enabled##rt", &rt.enabled);
        ImGui::BeginDisabled(!rt.enabled);

        int bounces = static_cast<int>(rt.maxBounces);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragInt("##rtbounces", &bounces, 0.25f, 0, 0, "Max Bounces: %d"))
            rt.maxBounces = static_cast<uint32_t>(std::max(1, bounces));

        int spp = static_cast<int>(rt.samplesPerPixel);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragInt("##rtspp", &spp, 0.25f, 0, 0, "Samples/Pixel: %d"))
            rt.samplesPerPixel = static_cast<uint32_t>(std::max(1, spp));

        ImGui::Checkbox("SVGF Denoise##rt", &rt.denoise);

        ImGui::EndDisabled();
    }

    ImGui::End();
}

} // namespace daedalus::editor
