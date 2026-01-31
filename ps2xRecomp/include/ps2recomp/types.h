#ifndef PS2RECOMP_TYPES_H
#define PS2RECOMP_TYPES_H

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <map>

namespace ps2recomp
{

    // Instruction representation
    struct Instruction
    {
        uint32_t address;
        uint32_t opcode;
        uint32_t rs;         // Source register
        uint32_t rt;         // Target register
        uint32_t rd;         // Destination register
        uint32_t sa;         // Shift amount
        uint32_t function;   // Function code for R-type instructions
        uint32_t immediate;  // Immediate value for I-type instructions
        uint32_t simmediate; // Sign-extended immediate value (extended to 32 bits)
        uint32_t target;     // Jump target for J-type instructions
        uint32_t raw;        // Raw instruction value

        // Instruction type flags
        bool isMMI;        // Is MMI instruction (PS2 specific)
        bool isVU;         // Is VU instruction (PS2 specific)
        bool isBranch;     // Is branch instruction
        bool isJump;       // Is jump instruction
        bool isCall;       // Is function call
        bool isReturn;     // Is return instruction
        bool hasDelaySlot; // Has delay slot
        bool isMultimedia; // PS2-specific multimedia operations
        bool isStore;      // Is store instruction
        bool isLoad;       // Is load instruction

        // Additional PS2-specific fields
        uint8_t mmiType;        // 0=MMI0, 1=MMI1, 2=MMI2, 3=MMI3
        uint8_t mmiFunction;    // Function within MMI type
        uint8_t pmfhlVariation; // For PMFHL instructions
        uint8_t vuFunction;     // For VU instructions

        struct
        {
            bool isVector;       // Uses vector operations
            bool usesQReg;       // Uses Q register
            bool usesPReg;       // Uses P register
            bool modifiesMAC;    // Modifies MAC flags
            uint8_t vectorField; // xyzw field mask
            uint8_t fsf;         // Field select for FS reg (bits 10-11)
            uint8_t ftf;         // Source Field select for FT reg (bits 8-9)
        } vectorInfo;

        struct
        {
            bool modifiesGPR;     // Modifies general purpose register
            bool modifiesFPR;     // Modifies floating point register
            bool modifiesVFR;     // Modifies vector float register
			bool modifiesVIR;     // Modifies vector integer register
			bool modifiesVIC;     // Modifies vector integer control register
            bool modifiesMemory;  // Modifies memory
            bool modifiesControl; // Modifies control register
        } modificationInfo;

        Instruction() : address(0), opcode(0), rs(0), rt(0), rd(0), sa(0), function(0),
                        immediate(0), simmediate(0), target(0), raw(0),
                        isMMI(false), isVU(false), isBranch(false), isJump(false), isCall(false),
                        isReturn(false), hasDelaySlot(false), isMultimedia(false), isStore(false), isLoad(false),
                        mmiType(0), mmiFunction(0), pmfhlVariation(0), vuFunction(0)
        {
            vectorInfo = {};
            modificationInfo = {};
        }
    };

    // Function information
    struct Function
    {
        std::string name;
        uint32_t start;
        uint32_t end;
        std::vector<Instruction> instructions;
        std::vector<uint32_t> callers;
        std::vector<uint32_t> callees;
        bool isRecompiled;
        bool isStub;
    };

    // Symbol information
    struct Symbol
    {
        std::string name;
        uint32_t address;
        uint32_t size;
        bool isFunction;
        bool isImported;
        bool isExported;
    };

    // Section information
    struct Section
    {
        std::string name;
        uint32_t address;
        uint32_t size;
        uint32_t offset;
        bool isCode;
        bool isData;
        bool isBSS;
        bool isReadOnly;
        uint8_t *data;
    };

    // Relocation information
    struct Relocation
    {
        uint32_t offset;
        uint32_t info;
        uint32_t symbol;
        uint32_t type;
        int32_t addend;
    };

    // Jump table entry
    struct JumpTableEntry
    {
        uint32_t index;
        uint32_t target;
    };

    // Jump table
    struct JumpTable
    {
        uint32_t address;
        uint32_t baseRegister;
        std::vector<JumpTableEntry> entries;
    };

    // Control flow graph
    struct CFGNode
    {
        uint32_t startAddress;
        uint32_t endAddress;
        std::vector<Instruction> instructions;
        std::vector<uint32_t> predecessors;
        std::vector<uint32_t> successors;
        bool isJumpTarget;
        bool hasJumpTable;
        JumpTable jumpTable;
    };

    // Function call
    struct FunctionCall
    {
        uint32_t callerAddress;
        uint32_t calleeAddress;
        std::string calleeName;
    };

    // Recompiler configuration
    struct RecompilerConfig
    {
        std::string inputPath;
        std::string outputPath;
        bool singleFileOutput;
        std::vector<std::string> skipFunctions;
        std::unordered_map<uint32_t, std::string> patches;
        std::vector<std::string> stubImplementations;
    };

} // namespace ps2recomp

#endif // PS2RECOMP_TYPES_H