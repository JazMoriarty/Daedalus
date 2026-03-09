#include "cmd_set_map_defaults.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetMapDefaults::CmdSetMapDefaults(EditMapDocument& doc,
                                     std::string      oldSkyPath,
                                     std::string      newSkyPath,
                                     float            oldGravity,
                                     float            newGravity,
                                     float            oldFloorH,
                                     float            newFloorH,
                                     float            oldCeilH,
                                     float            newCeilH)
    : m_doc(doc)
    , m_oldSkyPath(std::move(oldSkyPath))
    , m_newSkyPath(std::move(newSkyPath))
    , m_oldGravity(oldGravity)
    , m_newGravity(newGravity)
    , m_oldFloorH(oldFloorH)
    , m_newFloorH(newFloorH)
    , m_oldCeilH(oldCeilH)
    , m_newCeilH(newCeilH)
{}

void CmdSetMapDefaults::execute()
{
    m_doc.setSkyPath(m_newSkyPath);
    m_doc.setGravity(m_newGravity);
    m_doc.setDefaultFloorHeight(m_newFloorH);
    m_doc.setDefaultCeilHeight(m_newCeilH);
    m_doc.markDirty();
}

void CmdSetMapDefaults::undo()
{
    m_doc.setSkyPath(m_oldSkyPath);
    m_doc.setGravity(m_oldGravity);
    m_doc.setDefaultFloorHeight(m_oldFloorH);
    m_doc.setDefaultCeilHeight(m_oldCeilH);
    m_doc.markDirty();
}

} // namespace daedalus::editor
