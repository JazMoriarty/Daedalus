#include "output_log.h"
#include "daedalus/editor/edit_map_document.h"

#include "imgui.h"

#include <cstddef>

namespace daedalus::editor
{

void OutputLog::draw(const EditMapDocument& doc)
{
    ImGui::Begin("Output");

    const auto& msgs = doc.logMessages();

    // Clear button.
    if (ImGui::SmallButton("Clear"))
    {
        // Output log is read-only from the panel's perspective; we can't clear
        // it here without a non-const reference. Just reset scroll position.
        m_lastCount = msgs.size();
    }
    ImGui::SameLine();
    ImGui::Text("%zu messages", msgs.size());
    ImGui::Separator();

    // Scrollable region.
    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false,
                       ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& msg : msgs)
        ImGui::TextUnformatted(msg.c_str());

    // Auto-scroll when new messages arrive.
    if (msgs.size() != m_lastCount)
    {
        ImGui::SetScrollHereY(1.0f);
        m_lastCount = msgs.size();
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace daedalus::editor
