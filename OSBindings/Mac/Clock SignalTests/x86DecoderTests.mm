//
//  x86DecoderTests.m
//  Clock Signal
//
//  Created by Thomas Harte on 03/01/2021.
//  Copyright 2021 Thomas Harte. All rights reserved.
//

#import <XCTest/XCTest.h>

#include <initializer_list>
#include <vector>
#include "../../../Processors/Decoders/x86/x86.hpp"

namespace {
	using Operation = CPU::Decoder::x86::Operation;
	using Instruction = CPU::Decoder::x86::Instruction;
}

@interface x86DecoderTests : XCTestCase
@end

/*!
	Tests PowerPC decoding by throwing a bunch of randomly-generated
	word streams and checking that the result matches what I got from a
	disassembler elsewhere.
*/
@implementation x86DecoderTests {
	std::vector<Instruction> instructions;
}

// MARK: - Specific instruction asserts.

/* ... TODO ... */

// MARK: - Decoder

- (void)decode:(const std::initializer_list<uint8_t> &)stream {
	CPU::Decoder::x86::Decoder decoder(CPU::Decoder::x86::Model::i8086);

	// Start with a very dumb implementation: post one byte at a time.
	instructions.clear();
	for(auto item: stream) {
		const auto next = decoder.decode(&item, 1);
		if(next.size() > 0) {
			instructions.push_back(next);
		}
	}
}

// MARK: - Tests

- (void)testSequence1 {
	[self decode:{
		0x2d, 0x77, 0xea, 0x72, 0xfc, 0x4b, 0xb5, 0x28, 0xc3, 0xca, 0x26, 0x48, 0x65, 0x6d, 0x7b, 0x9f,
		0xc2, 0x65, 0x42, 0x4e, 0xef, 0x70, 0x20, 0x94, 0xc4, 0xd4, 0x93, 0x43, 0x3c, 0x8e, 0x6a, 0x65,
		0x1a, 0x78, 0x45, 0x10, 0x7f, 0x3c, 0x19, 0x5a, 0x16, 0x31, 0x64, 0x2c, 0xe7, 0xc6, 0x7d, 0xb0,
		0xb5, 0x49, 0x67, 0x61, 0xba, 0xc0, 0xcb, 0x14, 0x7e, 0x71, 0xd0, 0x50, 0x78, 0x3d, 0x03, 0x1d,
		0xe5, 0xc9, 0x97, 0xc3, 0x9b, 0xe6, 0xd3, 0x6c, 0x58, 0x4d, 0x76, 0x80, 0x44, 0xd6, 0x9f, 0xa5,
		0xbd, 0xa1, 0x12, 0xc5, 0x29, 0xc9, 0x9e, 0xd8, 0xf3, 0xcf, 0x92, 0x39, 0x5d, 0x90, 0x15, 0xc3,
		0xb8, 0xad, 0xe8, 0xc8, 0x16, 0x4a, 0xb0, 0x9e, 0xf9, 0xbf, 0x56, 0xea, 0x4e, 0xfd, 0xe4, 0x5a,
		0x23, 0xaa, 0x2c, 0x5b, 0x2a, 0xd2, 0xf7, 0x5f, 0x18, 0x86, 0x90, 0x25, 0x64, 0xb7, 0xc3
	}];
}

@end
