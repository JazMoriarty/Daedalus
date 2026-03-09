#include "map_doctor.h"
#include "daedalus/editor/edit_map_document.h"

#include <format>

namespace daedalus::editor
{

void runMapDoctor(EditMapDocument& doc)
{
    const auto issues = diagnose(doc.mapData());

    if (issues.empty())
    {
        doc.log("Map Doctor: no issues found.");
        return;
    }

    doc.log(std::format("Map Doctor: {} issue{} found:",
                        issues.size(), issues.size() == 1 ? "" : "s"));

    for (const auto& issue : issues)
        doc.log(LogEntry{"  " + issue.description, issue.jumpTo});
}

} // namespace daedalus::editor
