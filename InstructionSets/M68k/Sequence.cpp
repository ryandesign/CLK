//
//  Sequence.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 29/04/2022.
//  Copyright © 2022 Thomas Harte. All rights reserved.
//

#include "Sequence.hpp"

using namespace InstructionSet::M68k;

template <Step... T> struct Steps {
	static constexpr uint16_t value = 0;
};

template <Step F, Step... T> struct Steps<F, T...> {
	static constexpr uint16_t value = uint16_t(F) | uint16_t(Steps<T...>::value << 3);
};

template<Model model> uint16_t Sequence<model>::steps_for(Operation operation) {
	switch(operation) {
		// This handles a NOP, and not much else.
		default: return 0;

		//
		// Single operand, read-modify-write.
		//
		case Operation::NBCD:	return Steps< Step::FetchOp1, Step::Perform, Step::StoreOp1 >::value;

		//
		// Two operand, read-modify-write.
		//
		case Operation::ABCD:	case Operation::SBCD:
		case Operation::ADDb: 	case Operation::ADDw: 	case Operation::ADDl:
		case Operation::ADDAw:	case Operation::ADDAl:
		case Operation::ADDXb: 	case Operation::ADDXw: 	case Operation::ADDXl:
		case Operation::SUBb: 	case Operation::SUBw: 	case Operation::SUBl:
		case Operation::SUBAw:	case Operation::SUBAl:
		case Operation::SUBXb: 	case Operation::SUBXw: 	case Operation::SUBXl:
		return Steps< Step::FetchOp1, Step::FetchOp2, Step::Perform, Step::StoreOp2 >::value;

		//
		// Two operand, read-write.
		//
		case Operation::MOVEb: 	case Operation::MOVEw: 	case Operation::MOVEl:
		case Operation::MOVEAw:	case Operation::MOVEAl:
		return Steps< Step::FetchOp1, Step::Perform, Step::StoreOp2 >::value;
	}
}

template<Model model> Sequence<model>::Sequence(Operation operation) : steps_(steps_for(operation)) {}

template class InstructionSet::M68k::Sequence<Model::M68000>;
template class InstructionSet::M68k::Sequence<Model::M68010>;
template class InstructionSet::M68k::Sequence<Model::M68020>;
template class InstructionSet::M68k::Sequence<Model::M68030>;
template class InstructionSet::M68k::Sequence<Model::M68040>;
