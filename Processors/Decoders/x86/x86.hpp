//
//  x86.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 1/1/21.
//  Copyright © 2021 Thomas Harte. All rights reserved.
//

#ifndef x86_hpp
#define x86_hpp

#include <cstddef>
#include <cstdint>

namespace CPU {
namespace Decoder {
namespace x86 {

enum class Model {
	i8086,
};

enum class Operation: uint8_t {
	Invalid,

	/// ASCII adjust after addition; source will be AL and destination will be AX.
	AAA,
	/// ASCII adjust before division; destination will be AX and source will be a multiplier.
	AAD,
	/// ASCII adjust after multiplication; destination will be AX and source will be a divider.
	AAM,
	/// ASCII adjust after subtraction; source will be AL and destination will be AX.
	AAS,
	/// Add with carry; source, destination, operand and displacement will be populated appropriately.
	ADC,
	/// Add; source, destination, operand and displacement will be populated appropriately.
	ADD,
	/// And; source, destination, operand and displacement will be populated appropriately.
	AND,
	/// Far call; followed by a 32-bit operand.
	CALLF,
	/// Displacement call; followed by a 16-bit operand providing a call offset.
	CALLD,
	/* TODO: other CALLs */
	/// Convert byte into word; source will be AL, destination will be AH.
	CBW,
	/// Clear carry flag; no source or destination provided.
	CLC,
	/// Clear direction flag; no source or destination provided.
	CLD,
	/// Clear interrupt flag; no source or destination provided.
	CLI,
	/// Complement carry flag; no source or destination provided.
	CMC,
	/// Compare; source, destination, operand and displacement will be populated appropriately.
	CMP,
	/// Compare [bytes or words, per operation size]; source and destination implied to be DS:[SI] and ES:[DI].
	CMPS,
	/// Convert word to double word; source will be AX and destination will be DX.
	CWD,
	/// Decimal adjust after addition; source and destination will be AL.
	DAA,
	/// Decimal adjust after subtraction; source and destination will be AL.
	DAS,
	/// Dec; source, destination, operand and displacement will be populated appropriately.
	DEC,
	DIV, ESC, HLT, IDIV, IMUL, IN,
	INC, INT, INT3, INTO, IRET,
	JO,	JNO,	JB, JNB,	JE, JNE,	JBE, JNBE,
	JS, JNS,	JP, JNP,	JL, JNL,	JLE, JNLE,
	JMP, JCXZ,
	LAHF, LDS, LEA,
	LODS, LOOPE, LOOPNE, MOV, MOVS, MUL, NEG, NOP, NOT, OR, OUT,
	POP, POPF, PUSH, PUSHF, RCL, RCR, REP, ROL, ROR, SAHF,
	SAR, SBB, SCAS, SAL, SHR, STC, STD, STI, STOS, SUB, TEST,
	WAIT, XCHG, XLAT, XOR,
	LES, LOOP, JPCX,

	RETF,
	RETN,
};

enum class Size: uint8_t {
	Implied = 0,
	Byte = 1,
	Word = 2,
	DWord = 4,
};

enum class Source: uint8_t {
	None,

	AL, AH, AX,
	BL, BH, BX,
	CL, CH, CX,
	DL, DH, DX,

	CS, DS, ES, SS,
	SI, DI,
	BP, SP,

	IndBXPlusSI,
	IndBXPlusDI,
	IndBPPlusSI,
	IndBPPlusDI,
	IndSI,
	IndDI,
	DirectAddress,
	IndBP,
	IndBX,

	Immediate
};

enum class Repetition: uint8_t {
	None, RepE, RepNE
};

class Instruction {
	public:
		Operation operation = Operation::Invalid;
		Size operand_size = Size::Byte;

		Source source = Source::AL;
		Source destination = Source::AL;

		int size() const {
			return size_;
		}

		bool lock() const {
			return false;
		}

		Instruction() {}
		Instruction(int size) : size_(size) {}
		Instruction(Operation operation, Size operand_size, Source source, Source destination, int size) :
			operation(operation), operand_size(operand_size), source(source), destination(destination), size_(size) {}

	private:
		int size_ = -1;
		int16_t displacement_ = 0;
		int16_t operand_ = 0;
};

/*!
	Implements Intel x86 instruction decoding.

	This is an experimental implementation; it has not yet undergone significant testing.
*/
struct Decoder {
	public:
		Decoder(Model model);

		/*!
			@returns an @c Instruction with a positive size to indicate successful decoding; a
				negative size specifies the [negatived] number of further bytes the caller should ideally
				collect before calling again. The caller is free to call with fewer, but may not get a decoded
				instruction in response, and the decoder may still not be able to complete decoding
				even if given that number of bytes.
		*/
		Instruction decode(const uint8_t *source, size_t length);

	private:
		enum class Phase {
			/// Captures all prefixes and continues until an instruction byte is encountered.
			Instruction,
			/// Receives a ModRegRM byte and either populates the source_ and dest_ fields appropriately
			/// or completes decoding of the instruction, as per the instruction format.
			ModRegRM,
			/// Waits for sufficiently many bytes to pass for the required displacement and operand to be captured.
			/// Cf. displacement_size_ and operand_size_.
			AwaitingDisplacementOrOperand,
			/// Forms and returns an Instruction, and resets parsing state.
			ReadyToPost
		} phase_ = Phase::Instruction;

		/// During the ModRegRM phase, format dictates interpretation of the ModRegRM byte.
		///
		/// During the ReadyToPost phase, format determines how transiently-recorded fields
		/// are packaged into an Instruction.
		enum class ModRegRMFormat: uint8_t {
			// Parse the ModRegRM for mode, register and register/memory fields
			// and populate the source_ and destination_ fields appropriate.
			MemReg_Reg,
			Reg_MemReg,

			// Parse for mode and register/memory fields, populating both
			// source_ and destination_ fields with the result. Use the 'register'
			// field to pick an operation from the TEST/NOT/NEG/MUL/IMUL/DIV/IDIV group.
			MemRegTEST_to_IDIV,

			// Parse for mode and register/memory fields, populating both
			// source_ and destination_ fields with the result. Use the 'register'
			// field to check for the POP operation.
			MemRegPOP,

			// Parse for mode and register/memory fields, populating both
			// the destination_ field with the result and setting source_ to Immediate.
			// Use the 'register' field to check for the MOV operation.
			MemRegMOV,

			// Parse for mode and register/memory fields, populating the
			// destination_ field with the result. Use the 'register' field
			// to pick an operation from the ROL/ROR/RCL/RCR/SAL/SHR/SAR group.
			MemRegROL_to_SAR,

			// Parse for mode and register/memory fields, populating the
			// source_ field with the result. Fills destination_ with a segment
			// register based on the reg field.
			SegReg,

		} modregrm_format_ = ModRegRMFormat::MemReg_Reg;

		// Ephemeral decoding state.
		Operation operation_ = Operation::Invalid;
		uint8_t instr_ = 0x00;	// TODO: is this desired, versus loading more context into ModRegRMFormat?
		int consumed_ = 0, operand_bytes_ = 0;

		// Source and destination locations.
		Source source_ = Source::None;
		Source destination_ = Source::None;

		// Facts about the instruction.
		int displacement_size_ = 0;		// i.e. size of in-stream displacement, if any.
		int operand_size_ = 0;			// i.e. size of in-stream operand, if any.
		int operation_size_ = 0;		// i.e. size of data manipulated by the operation.

		// Prefix capture fields.
		Repetition repetition_ = Repetition::None;
		bool lock_ = false;
		Source segment_override_ = Source::None;

		/// Resets size capture and all fields with default values.
		void reset_parsing() {
			consumed_ = operand_bytes_ = 0;
			displacement_size_ = operand_size_ = 0;
			lock_ = false;
			segment_override_ = Source::None;
			repetition_ = Repetition::None;
		}
};

}
}
}

#endif /* x86_hpp */
