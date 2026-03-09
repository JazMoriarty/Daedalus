#pragma once

#include "daedalus/editor/i_command.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Changes the yaw (Y-axis rotation, radians) of an existing entity.
/// Undo restores the previous yaw.
class CmdRotateEntity final : public ICommand
{
public:
    CmdRotateEntity(EditMapDocument& doc,
                    std::size_t      entityIndex,
                    float            oldYaw,
                    float            newYaw);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Rotate Entity"; }

private:
    EditMapDocument& m_doc;
    std::size_t      m_idx;
    float            m_oldYaw;
    float            m_newYaw;
};

} // namespace daedalus::editor
