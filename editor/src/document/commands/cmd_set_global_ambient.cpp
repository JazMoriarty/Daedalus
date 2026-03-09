#include "cmd_set_global_ambient.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetGlobalAmbient::CmdSetGlobalAmbient(EditMapDocument& doc,
                                         glm::vec3        oldColor,
                                         glm::vec3        newColor,
                                         float            oldIntensity,
                                         float            newIntensity)
    : m_doc(doc)
    , m_oldColor(oldColor)
    , m_newColor(newColor)
    , m_oldIntensity(oldIntensity)
    , m_newIntensity(newIntensity)
{}

void CmdSetGlobalAmbient::execute()
{
    m_doc.mapData().globalAmbientColor     = m_newColor;
    m_doc.mapData().globalAmbientIntensity = m_newIntensity;
    m_doc.markDirty();
}

void CmdSetGlobalAmbient::undo()
{
    m_doc.mapData().globalAmbientColor     = m_oldColor;
    m_doc.mapData().globalAmbientIntensity = m_oldIntensity;
    m_doc.markDirty();
}

} // namespace daedalus::editor
