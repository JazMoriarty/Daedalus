#include "output_log.h"
#include "daedalus/editor/edit_map_document.h"

#include "imgui.h"

#include <cstddef>

namespace daedalus::editor
{

void OutputLog::draw(EditMapDocument& doc)
{
    ImGui::Begin("Output");

    const auto& msgs = doc.logMessages();

    // Clear button — now functional via clearLog().
    if (ImGui::SmallButton("Clear"))
    {
        doc.clearLog();
        m_lastCount = 0;
    }
    ImGui::SameLine();
    ImGui::Text("%zu messages", msgs.size());
    ImGui::Separator();

    // Scrollable region.
    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false,
                       ImGuiWindowFlags_HorizontalScrollbar);

    for (std::size_t i = 0; i < msgs.size(); ++i)
    {
        const LogEntry& entry = msgs[i];

        // Jump-to button for entries that carry a selection target.
        if (entry.jumpTo.has_value())
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("\xe2\x86\x92"))  // UTF-8 right arrow →
                doc.selection() = entry.jumpTo.value();
            ImGui::SameLine();
            ImGui::PopID();
        }

        ImGui::TextUnformatted(entry.message.c_str());
    }

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
