//
//Copyright (C) 2014 LunarG, Inc.
//
//All rights reserved.
//
//Redistribution and use in source and binary forms, with or without
//modification, are permitted provided that the following conditions
//are met:
//
//    Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
//THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
//ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//POSSIBILITY OF SUCH DAMAGE.

//
// Author: John Kessenich, LunarG
//

// SPIRV-IR
//
// Simple in-memory representation (IR) of SPIRV.  Just for holding
// Each function's CFG of blocks.  Has this hierarchy:
//  - Module, which is a list of 
//    - Function, which is a list of 
//      - Block, which is a list of 
//        - Instruction
//

#pragma once
#ifndef spvIR_H
#define spvIR_H

#include "spirv.hpp"

#include <vector>
#include <iostream>
#include <assert.h>

namespace spv {

class Function;
class Module;

const Id NoResult = 0;
const Id NoType = 0;

const unsigned int BadValue = 0xFFFFFFFF;
const Decoration NoPrecision = (Decoration)BadValue;
const MemorySemanticsMask MemorySemanticsAllMemory = (MemorySemanticsMask)0x3FF;

//
// SPIR-V IR instruction.
//

class Instruction {
public:
    Instruction(Id resultId, Id typeId, Op opCode) : resultId(resultId), typeId(typeId), opCode(opCode), string(0) { }
    explicit Instruction(Op opCode) : resultId(NoResult), typeId(NoType), opCode(opCode), string(0) { }
    virtual ~Instruction()
    {
        delete string;
    }
    void addIdOperand(Id id) { operands.push_back(id); }
    void addImmediateOperand(unsigned int immediate) { operands.push_back(immediate); }
    void addStringOperand(const char* str)
    {
        originalString = str;
        string = new std::vector<unsigned int>;
        unsigned int word;
        char* wordString = (char*)&word;
        char* wordPtr = wordString;
        int charCount = 0;
        char c;
        do {
            c = *(str++);
            *(wordPtr++) = c;
            ++charCount;
            if (charCount == 4) {
                string->push_back(word);
                wordPtr = wordString;
                charCount = 0;
            }
        } while (c != 0);

        // deal with partial last word
        if (charCount > 0) {
            // pad with 0s
            for (; charCount < 4; ++charCount)
                *(wordPtr++) = 0;
            string->push_back(word);
        }
    }
    Op getOpCode() const { return opCode; }
    int getNumOperands() const { return (int)operands.size(); }
    Id getResultId() const { return resultId; }
    Id getTypeId() const { return typeId; }
    Id getIdOperand(int op) const { return operands[op]; }
    unsigned int getImmediateOperand(int op) const { return operands[op]; }
    const char* getStringOperand() const { return originalString.c_str(); }
    void rewriteOperands(Id oldOperand, Id newOperand)
    {
        for(auto i = 0u; i < operands.size(); ++i)
        {
            rewriteOperand(oldOperand, newOperand, i);
        }
    }
    void rewriteOperand(Id oldOperand, Id newOperand, unsigned int index)
    {
        if(operands[index] == oldOperand)
            operands[index] = newOperand;
    }

    // Write out the binary form.
    void dump(std::vector<unsigned int>& out) const
    {
        // Compute the wordCount
        unsigned int wordCount = 1;
        if (typeId)
            ++wordCount;
        if (resultId)
            ++wordCount;
        wordCount += (unsigned int)operands.size();
        if (string)
            wordCount += (unsigned int)string->size();

        // Write out the beginning of the instruction
        out.push_back(((wordCount) << WordCountShift) | opCode);
        if (typeId)
            out.push_back(typeId);
        if (resultId)
            out.push_back(resultId);

        // Write out the operands
        for (int op = 0; op < (int)operands.size(); ++op)
            out.push_back(operands[op]);
        if (string)
            for (int op = 0; op < (int)string->size(); ++op)
                out.push_back((*string)[op]);
    }

protected:
    Instruction(const Instruction&);
    Id resultId;
    Id typeId;
    Op opCode;
    std::vector<Id> operands;
    std::vector<unsigned int>* string; // usually non-existent
    std::string originalString;        // could be optimized away; convenience for getting string operand
};

//
// SPIR-V IR block.
//

class Block {
  using Instructions = std::vector<Instruction*>;

public:
    Block(Id id, Function& parent);
    virtual ~Block()
    {
        // TODO: free instructions
    }
    
    Id getId() { return instructions.front()->getResultId(); }

    Function& getParent() const { return parent; }
    void addInstruction(Instruction* inst);
    // Insert a new instruction at the front of the block after the label
    void addInstructionAtFront(Instruction* inst);
    // Insert an instruction either at the end of the block, or prepend it in
    // front of potentially existing terminating instructions.
    void insertInstructionBeforeTerminal(Instruction* inst);
    void addPredecessor(Block* pred) { predecessors.push_back(pred); }
    void addSuccessor(Block* succ) { successors.push_back(succ); }
    void addLocalVariable(Instruction* inst) { localVariables.push_back(inst); }
    int getNumPredecessors() const { return (int)predecessors.size(); }
    int getNumSuccessors() const { return (int)successors.size(); }
    int getNumInstructions() const { return (int)instructions.size(); }
    void setUnreachable() { unreachable = true; }
    bool isUnreachable() const { return unreachable; }
    bool isPredecessorOf(const Block* pred) const
    {
        return pred && pred->hasPredecessor(this);
    }
    bool hasPredecessor(const Block* pred) const
    {
        return std::find(predecessors.begin(), predecessors.end(), pred) != predecessors.end();
    }
    Block* getSuccessor(int i) const { return successors[i]; }

    // Iterators over the block's instruction stream
    Instructions::iterator begin() { return instructions.begin(); }
    Instructions::iterator end() { return instructions.end(); }
    Instructions::const_iterator begin() const { return instructions.begin(); }
    Instructions::const_iterator end() const { return instructions.end(); }

    bool isTerminated() const
    {
        switch (instructions.back()->getOpCode()) {
        case OpBranch:
        case OpBranchConditional:
        case OpSwitch:
        case OpKill:
        case OpReturn:
        case OpReturnValue:
            return true;
        default:
            return false;
        }
    }

    void dump(std::vector<unsigned int>& out) const
    {
        // skip the degenerate unreachable blocks
        // TODO: code gen: skip all unreachable blocks (transitive closure)
        //                 (but, until that's done safer to keep non-degenerate unreachable blocks, in case others depend on something)
        if (unreachable && instructions.size() <= 2)
            return;

        instructions[0]->dump(out);
        for (int i = 0; i < (int)localVariables.size(); ++i)
            localVariables[i]->dump(out);
        for (int i = 1; i < (int)instructions.size(); ++i)
            instructions[i]->dump(out);
    }

protected:
    Block(const Block&);
    Block& operator=(Block&);

    // To enforce keeping parent and ownership in sync:
    friend Function;

    Instructions instructions;
    std::vector<Block*> predecessors;
    std::vector<Block*> successors;
    std::vector<Instruction*> localVariables;
    Function& parent;

    // track whether this block is known to be uncreachable (not necessarily 
    // true for all unreachable blocks, but should be set at least
    // for the extraneous ones introduced by the builder).
    bool unreachable;
};

//
// SPIR-V IR Function.
//

class Function {
public:
    Function(Id id, Id resultType, Id functionType, Id firstParam, Module& parent);
    virtual ~Function()
    {
        for (int i = 0; i < (int)parameterInstructions.size(); ++i)
            delete parameterInstructions[i];

        for (int i = 0; i < (int)blocks.size(); ++i)
            delete blocks[i];
    }
    Id getId() const { return functionInstruction.getResultId(); }
    Id getParamId(int p) { return parameterInstructions[p]->getResultId(); }

    void addBlock(Block* block) { blocks.push_back(block); }
    void popBlock(Block*) { blocks.pop_back(); }

    Module& getParent() const { return parent; }
    Block* getEntryBlock() const { return blocks.front(); }
    Block* getLastBlock() const { return blocks.back(); }
    void addLocalVariable(Instruction* inst);
    Id getReturnType() const { return functionInstruction.getTypeId(); }
    void dump(std::vector<unsigned int>& out) const
    {
        // OpFunction
        functionInstruction.dump(out);

        // OpFunctionParameter
        for (int p = 0; p < (int)parameterInstructions.size(); ++p)
            parameterInstructions[p]->dump(out);

        // Blocks
        for (int b = 0; b < (int)blocks.size(); ++b)
            blocks[b]->dump(out);
        Instruction end(0, 0, OpFunctionEnd);
        end.dump(out);
    }

protected:
    Function(const Function&);
    Function& operator=(Function&);

    Module& parent;
    Instruction functionInstruction;
    std::vector<Instruction*> parameterInstructions;
    std::vector<Block*> blocks;
};

//
// SPIR-V IR Module.
//

class Module {
public:
    Module() {}
    Module(Module&&) = default;
    Module& operator=(Module&&) = default;
    virtual ~Module()
    {
        // TODO delete things
    }

    void addFunction(Function *fun) { functions.push_back(fun); }

    void mapInstruction(Instruction *instruction)
    {
        spv::Id resultId = instruction->getResultId();
        // map the instruction's result id
        if (resultId >= idToInstruction.size())
            idToInstruction.resize(resultId + 16);
        idToInstruction[resultId] = instruction;
    }

    Instruction* getInstruction(Id id) const { return idToInstruction[id]; }
    spv::Id getTypeId(Id resultId) const { return idToInstruction[resultId]->getTypeId(); }
    StorageClass getStorageClass(Id typeId) const { return (StorageClass)idToInstruction[typeId]->getImmediateOperand(0); }
    void dump(std::vector<unsigned int>& out) const
    {
        for (int f = 0; f < (int)functions.size(); ++f)
            functions[f]->dump(out);
    }

protected:
    Module(const Module&);
    std::vector<Function*> functions;

    // map from result id to instruction having that result id
    std::vector<Instruction*> idToInstruction;

    // map from a result id to its type id
};

//
// Implementation (it's here due to circular type definitions).
//

// Add both
// - the OpFunction instruction
// - all the OpFunctionParameter instructions
__inline Function::Function(Id id, Id resultType, Id functionType, Id firstParamId, Module& parent)
    : parent(parent), functionInstruction(id, resultType, OpFunction)
{
    // OpFunction
    functionInstruction.addImmediateOperand(FunctionControlMaskNone);
    functionInstruction.addIdOperand(functionType);
    parent.mapInstruction(&functionInstruction);
    parent.addFunction(this);

    // OpFunctionParameter
    Instruction* typeInst = parent.getInstruction(functionType);
    int numParams = typeInst->getNumOperands() - 1;
    for (int p = 0; p < numParams; ++p) {
        Instruction* param = new Instruction(firstParamId + p, typeInst->getIdOperand(p + 1), OpFunctionParameter);
        parent.mapInstruction(param);
        parameterInstructions.push_back(param);
    }
}

__inline void Function::addLocalVariable(Instruction* inst)
{
    blocks[0]->addLocalVariable(inst);
    parent.mapInstruction(inst);
}

__inline Block::Block(Id id, Function& parent) : parent(parent), unreachable(false)
{
    instructions.push_back(new Instruction(id, NoType, OpLabel));
}

__inline void Block::addInstruction(Instruction* inst)
{
    instructions.push_back(inst);
    if (inst->getResultId())
        parent.getParent().mapInstruction(inst);
}

inline void Block::addInstructionAtFront(Instruction* inst)
{
    assert(!instructions.empty());
    assert(instructions.front()->getOpCode() == OpLabel);
    assert(inst->getOpCode() != OpLabel);
    instructions.insert(++instructions.begin(), inst);
    if (inst->getResultId())
        parent.getParent().mapInstruction(inst);
}


inline void Block::insertInstructionBeforeTerminal(Instruction* inst)
{
    switch (instructions.back()->getOpCode()) {
    case OpBranchConditional:
    case OpSwitch:
        instructions.insert(instructions.end() - 2, inst);
        break;
    case OpBranch:
        if(instructions.size() > 1 && (instructions[instructions.size() - 2]->getOpCode() == OpLoopMerge))
            instructions.insert(instructions.end() - 2, inst);
        else
            instructions.insert(instructions.end() - 1, inst);
        break;
    case OpKill:
    case OpReturn:
    case OpReturnValue:
        instructions.insert(--instructions.end(), inst);
        break;
    default:
        instructions.push_back(inst);
        break;
    }
}

};  // end spv namespace

#endif // spvIR_H
