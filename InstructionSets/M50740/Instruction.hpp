//
//  Instruction.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 15/01/21.
//  Copyright © 2021 Thomas Harte. All rights reserved.
//

#ifndef InstructionSets_M50740_Instruction_h
#define InstructionSets_M50740_Instruction_h

#include <cstdint>
#include "../AccessType.hpp"

namespace InstructionSet {
namespace M50740 {

enum class AddressingMode {
	Implied,				Accumulator,			Immediate,
	Absolute,				AbsoluteX,				AbsoluteY,
	ZeroPage,				ZeroPageX,				ZeroPageY,
	XIndirect,				IndirectY,
	Relative,
	AbsoluteIndirect,		ZeroPageIndirect,
	SpecialPage,
	ImmediateZeroPage,
	AccumulatorRelative,	ZeroPageRelative
};

static constexpr auto MaxAddressingMode = int(AddressingMode::ZeroPageRelative);
static constexpr auto MinAddressingMode = int(AddressingMode::Implied);

constexpr int size(AddressingMode mode) {
	// This is coupled to the AddressingMode list above; be careful!
	constexpr int sizes[] = {
		0, 0, 1,
		2, 2, 2,
		1, 1, 1,
		1, 1,
		1,
		2, 1,
		1,
		2,
		1,	2
	};
	static_assert(sizeof(sizes)/sizeof(*sizes) == int(MaxAddressingMode) + 1);
	return sizes[int(mode)];
}

enum class Operation: uint8_t {
	Invalid,

	// Operations that don't access memory.
	BBC0,	BBS0,	BBC1,	BBS1,	BBC2,	BBS2,	BBC3,	BBS3,
	BBC4,	BBS4,	BBC5,	BBS5,	BBC6,	BBS6,	BBC7,	BBS7,
	BCC,	BCS,
	BEQ,	BMI,	BNE,	BPL,
	BVC,	BVS,	BRA,	BRK,
	JMP,	JSR,
	RTI,	RTS,
	CLC,	CLD,	CLI,	CLT,	CLV,
	SEC,	SED,	SEI,	SET,
	INX,	INY,	DEX,	DEY,
	FST,	SLW,
	NOP,
	PHA, 	PHP, 	PLA,	PLP,
	STP,
	TAX,	TAY,	TSX,	TXA,
	TXS,	TYA,

	// Read operations.
	ADC,	SBC,
	AND,	ORA,	EOR,	BIT,
	CMP,	CPX,	CPY,
	LDA,	LDX,	LDY,
	TST,

	// Read-modify-write operations.
	ASL,	LSR,
	CLB0,	SEB0,	CLB1,	SEB1,	CLB2,	SEB2,	CLB3,	SEB3,
	CLB4,	SEB4,	CLB5,	SEB5,	CLB6,	SEB6,	CLB7,	SEB7,
	COM,
	DEC,	INC,
	ROL,	ROR,	RRF,

	// Write operations.
	LDM,
	STA,	STX,	STY,
};

static constexpr auto MaxOperation = int(Operation::STY);
static constexpr auto MinOperation = int(Operation::BBC0);

constexpr AccessType access_type(Operation operation) {
	if(operation < Operation::ADC)	return AccessType::None;
	if(operation < Operation::ASL)	return AccessType::Read;
	if(operation < Operation::LDM)	return AccessType::Write;
	return AccessType::ReadModifyWrite;
}

struct Instruction {
	Operation operation = Operation::Invalid;
	AddressingMode addressing_mode = AddressingMode::Implied;
	uint8_t opcode = 0;

	Instruction(Operation operation, AddressingMode addressing_mode, uint8_t opcode) : operation(operation), addressing_mode(addressing_mode), opcode(opcode) {}
	Instruction(uint8_t opcode) : opcode(opcode) {}
	Instruction() {}
};

}
}


#endif /* InstructionSets_M50740_Instruction_h */
