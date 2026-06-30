#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include <fmt/format.h>
#include <sstream>
#include <cmath>

namespace ps2recomp
{
    std::string CodeGenerator::generateJumpTableSwitch(const Instruction &inst, uint32_t tableAddress,
                                                       const std::vector<JumpTableEntry> &entries)
    {
        std::stringstream ss;

        uint32_t indexReg = inst.rs;

        ss << "switch (GPR_U32(ctx, " << indexReg << ")) {\n";

        for (const auto &[index, target] : entries)
        {
            ss << "    case " << index << ": {\n";

            std::string funcName = getFunctionName(target);
            if (!funcName.empty())
            {
                ss << "        " << funcName << "(rdram, ctx, runtime);\n";
            }
            else
            {
                ss << "        func_" << std::hex << target << std::dec << "(rdram, ctx,  runtime);\n";
            }

            ss << "        return;\n";
            ss << "    }\n";
        }

        ss << "    default:\n";
        ss << "        // Unknown jump table target\n";
        ss << "        return;\n";
        ss << "}\n";

        return ss.str();
    }

}
