//
//  ZXSpectrum.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 17/03/2021.
//  Copyright © 2021 Thomas Harte. All rights reserved.
//

#include "ZXSpectrum.hpp"

#include "Video.hpp"

#define LOG_PREFIX "[Spectrum] "

#include "../../MachineTypes.hpp"

#include "../../../Processors/Z80/Z80.hpp"

#include "../../../Components/AudioToggle/AudioToggle.hpp"
#include "../../../Components/AY38910/AY38910.hpp"

#include "../../../Outputs/Log.hpp"
#include "../../../Outputs/Speaker/Implementation/CompoundSource.hpp"
#include "../../../Outputs/Speaker/Implementation/LowpassSpeaker.hpp"
#include "../../../Outputs/Speaker/Implementation/SampleSource.hpp"

#include "../../../Analyser/Static/ZXSpectrum/Target.hpp"

#include "../../Utility/MemoryFuzzer.hpp"

#include "../../../ClockReceiver/JustInTime.hpp"

#include "../Keyboard/Keyboard.hpp"

#include <array>

namespace Sinclair {
namespace ZXSpectrum {

using Model = Analyser::Static::ZXSpectrum::Target::Model;
template<Model model> class ConcreteMachine:
	public Configurable::Device,
	public Machine,
	public MachineTypes::AudioProducer,
	public MachineTypes::MappedKeyboardMachine,
	public MachineTypes::MediaTarget,
	public MachineTypes::ScanProducer,
	public MachineTypes::TimedMachine,
	public CPU::Z80::BusHandler {
	public:
		ConcreteMachine(const Analyser::Static::ZXSpectrum::Target &target, const ROMMachine::ROMFetcher &rom_fetcher) :
			z80_(*this),
			ay_(GI::AY38910::Personality::AY38910, audio_queue_),
			audio_toggle_(audio_queue_),
			mixer_(ay_, audio_toggle_),
			speaker_(mixer_),
			keyboard_(Sinclair::ZX::Keyboard::Machine::ZXSpectrum),
			keyboard_mapper_(Sinclair::ZX::Keyboard::Machine::ZXSpectrum),
			tape_player_(clock_rate() * 2)
		{
			set_clock_rate(clock_rate());
			speaker_.set_input_rate(float(clock_rate()) / 2.0f);

			// With only the +2a and +3 currently supported, the +3 ROM is always
			// the one required.
			const auto roms =
				rom_fetcher({ {"ZXSpectrum", "the +2a/+3 ROM", "plus3.rom", 64 * 1024, 0x96e3c17a} });
			if(!roms[0]) throw ROMMachine::Error::MissingROMs;
			memcpy(rom_.data(), roms[0]->data(), std::min(rom_.size(), roms[0]->size()));

			// Set up initial memory map.
			update_memory_map();
			Memory::Fuzz(ram_);

			// Insert media.
			insert_media(target.media);
		}

		~ConcreteMachine() {
			audio_queue_.flush();
		}

		static constexpr unsigned int clock_rate() {
//			constexpr unsigned int ClockRate = 3'500'000;
			constexpr unsigned int Plus3ClockRate = 3'546'900;
			return Plus3ClockRate;
		}

		// MARK: - TimedMachine

		void run_for(const Cycles cycles) override {
			z80_.run_for(cycles);
		}

		void flush() {
			video_.flush();
			update_audio();
			audio_queue_.perform();
		}

		// MARK: - ScanProducer

		void set_scan_target(Outputs::Display::ScanTarget *scan_target) override {
			video_->set_scan_target(scan_target);
		}

		Outputs::Display::ScanStatus get_scaled_scan_status() const override {
			return video_->get_scaled_scan_status();
		}

		void set_display_type(Outputs::Display::DisplayType display_type) override {
			video_->set_display_type(display_type);
		}

		// MARK: - BusHandler

		forceinline HalfCycles perform_machine_cycle(const CPU::Z80::PartialMachineCycle &cycle) {
			// Ignore all but terminal cycles.
			// TODO: I doubt this is correct for timing.
			if(!cycle.is_terminal()) {
				advance(cycle.length);
				return HalfCycles(0);
			}

			HalfCycles delay(0);
			uint16_t address = cycle.address ? *cycle.address : 0x0000;
			using PartialMachineCycle = CPU::Z80::PartialMachineCycle;
			switch(cycle.operation) {
				default: break;
				case PartialMachineCycle::ReadOpcode:
				case PartialMachineCycle::Read:
					// Apply contention if necessary.
					if(is_contended_[address >> 14]) {
						delay = video_.last_valid()->access_delay(video_.time_since_flush());
					}

					*cycle.value = read_pointers_[address >> 14][address];
				break;

				case PartialMachineCycle::Write:
					// Apply contention if necessary.
					// For now this causes a video sync up every time any contended area is written to.
					// TODO: flush only upon a video-area write.
					if(is_contended_[address >> 14]) {
						delay = video_->access_delay(HalfCycles(0));
					}

					write_pointers_[address >> 14][address] = *cycle.value;
				break;

				case PartialMachineCycle::Output:
					// Test for port FE.
					if(!(address&1)) {
						update_audio();
						audio_toggle_.set_output(*cycle.value & 0x10);

						video_->set_border_colour(*cycle.value & 7);

						// b0–b2: border colour
						// b3: enable tape input (?)
						// b4: tape and speaker output
					}

					// Test for classic 128kb paging.
//					if(!(address&0x8002)) {
//					}

					// Test for +2a/+3 paging.
//					if((address & 0xc002) == 0x4000) {
//					}

					switch(address) {
						default: break;

						case 0x1ffd:
							// Write to +2a/+3 paging register.
							port1ffd_ = *cycle.value;
							update_memory_map();
						break;

						case 0x7ffd:
							// Write to classic 128kb paging register.
							disable_paging_ |= *cycle.value & 0x20;
							port7ffd_ = *cycle.value;
							update_memory_map();
						break;

						case 0xfffd:
							// Select AY register.
							update_audio();
							ay_.set_control_lines(GI::AY38910::ControlLines(GI::AY38910::BDIR | GI::AY38910::BC2 | GI::AY38910::BC1));
							ay_.set_data_input(*cycle.value);
							ay_.set_control_lines(GI::AY38910::ControlLines(0));
						break;

						case 0xbffd:
							// Write to AY register.
							update_audio();
							ay_.set_control_lines(GI::AY38910::ControlLines(GI::AY38910::BDIR | GI::AY38910::BC2));
							ay_.set_data_input(*cycle.value);
							ay_.set_control_lines(GI::AY38910::ControlLines(0));
						break;
					}
				break;

				case PartialMachineCycle::Input:
					*cycle.value = 0xff;

					if(!(address&1)) {
						// Port FE:
						//
						// address b8+: mask of keyboard lines to select
						// result: b0–b4: mask of keys pressed
						// b6: tape input

						*cycle.value &= keyboard_.read(address);
						*cycle.value &= tape_player_.get_input() ? 0xbf : 0xff;

						// If this read is within 200 cycles of the previous,
						// count it as an adjacent hit; if 20 of those have
						// occurred then start the tape motor.
						if(use_automatic_tape_motor_control_) {
							if(cycles_since_tape_input_read_ < HalfCycles(400)) {
								++recent_tape_hits_;

								if(recent_tape_hits_ == 20) {
									tape_player_.set_motor_control(true);
								}
							} else {
								recent_tape_hits_ = 0;
							}

							cycles_since_tape_input_read_ = HalfCycles(0);
						}
					}

					switch(address) {
						default: break;

						case 0xfffd:
							// Read from AY register.
							update_audio();
							ay_.set_control_lines(GI::AY38910::ControlLines(GI::AY38910::BC2 | GI::AY38910::BC1));
							*cycle.value &= ay_.get_data_output();
							ay_.set_control_lines(GI::AY38910::ControlLines(0));
						break;
					}
				break;
			}

			advance(cycle.length + delay);
			return delay;
		}

	private:
		void advance(HalfCycles duration) {
			time_since_audio_update_ += duration;

			video_ += duration;
			if(video_.did_flush()) {
				z80_.set_interrupt_line(video_.last_valid()->get_interrupt_line());
			}

			// TODO: sleeping support here.
			tape_player_.run_for(duration.as_integral());

			// Update automatic tape motor control, if enabled; if it's been
			// 3 seconds since software last possibly polled the tape, stop it.
			if(use_automatic_tape_motor_control_ && cycles_since_tape_input_read_ < HalfCycles(clock_rate() * 6)) {
				cycles_since_tape_input_read_ += duration;

				if(cycles_since_tape_input_read_ >= HalfCycles(clock_rate() * 6)) {
					tape_player_.set_motor_control(false);
					recent_tape_hits_ = 0;
				}
			}
		}

	public:

		// MARK: - Typer
//		HalfCycles get_typer_delay(const std::string &) const final {
//			return z80_.get_is_resetting() ? Cycles(7'000'000) : Cycles(0);
//		}
//
//		HalfCycles get_typer_frequency() const final {
//			return Cycles(146'250);
//		}

		KeyboardMapper *get_keyboard_mapper() override {
			return &keyboard_mapper_;
		}

		// MARK: - Keyboard
		void set_key_state(uint16_t key, bool is_pressed) override {
			keyboard_.set_key_state(key, is_pressed);
		}

		void clear_all_keys() override {
			keyboard_.clear_all_keys();
		}

		// MARK: - MediaTarget.
		bool insert_media(const Analyser::Static::Media &media) override {
			// If there are any tapes supplied, use the first of them.
			if(!media.tapes.empty()) {
				tape_player_.set_tape(media.tapes.front());
			}

			return !media.tapes.empty();
		}

		// MARK: - Tape control

		void set_use_automatic_tape_motor_control(bool enabled) {
			use_automatic_tape_motor_control_ = enabled;
			if(!enabled) {
				tape_player_.set_motor_control(false);
			}
		}

		void set_tape_is_playing(bool is_playing) final {
			tape_player_.set_motor_control(is_playing);
		}

		bool get_tape_is_playing() final {
			return tape_player_.get_motor_control();
		}

		// MARK: - Configuration options.

		std::unique_ptr<Reflection::Struct> get_options() override {
			auto options = std::make_unique<Options>(Configurable::OptionsType::UserFriendly);	// OptionsType is arbitrary, but not optional.
			options->automatic_tape_motor_control = use_automatic_tape_motor_control_;
			options->quickload = allow_fast_tape_hack_;
			return options;
		}

		void set_options(const std::unique_ptr<Reflection::Struct> &str) override {
			const auto options = dynamic_cast<Options *>(str.get());
			set_video_signal_configurable(options->output);
			set_use_automatic_tape_motor_control(options->automatic_tape_motor_control);
			allow_fast_tape_hack_ = options->quickload;
			set_use_fast_tape();
		}

		// MARK: - AudioProducer.

		Outputs::Speaker::Speaker *get_speaker() override {
			return &speaker_;
		}

	private:
		CPU::Z80::Processor<ConcreteMachine, false, false> z80_;

		// MARK: - Memory.
		std::array<uint8_t, 64*1024> rom_;
		std::array<uint8_t, 128*1024> ram_;

		std::array<uint8_t, 16*1024> scratch_;
		const uint8_t *read_pointers_[4];
		uint8_t *write_pointers_[4];
		bool is_contended_[4];

		uint8_t port1ffd_ = 0;
		uint8_t port7ffd_ = 0;
		bool disable_paging_ = false;

		void update_memory_map() {
			// If paging is permanently disabled, don't react.
			if(disable_paging_) {
				return;
			}

			// Set the proper video base pointer.
			video_->set_video_source(&ram_[((port7ffd_ & 0x08) ? 7 : 5) * 16384]);

			if(port1ffd_ & 1) {
				// "Special paging mode", i.e. one of four fixed
				// RAM configurations, port 7ffd doesn't matter.

				switch(port1ffd_ & 0x6) {
					default:
					case 0x00:
						set_memory(0, &ram_[0 * 16384], &ram_[0 * 16384], false);
						set_memory(1, &ram_[1 * 16384], &ram_[1 * 16384], false);
						set_memory(2, &ram_[2 * 16384], &ram_[2 * 16384], false);
						set_memory(3, &ram_[3 * 16384], &ram_[3 * 16384], false);
					break;

					case 0x02:
						set_memory(0, &ram_[4 * 16384], &ram_[4 * 16384], true);
						set_memory(1, &ram_[5 * 16384], &ram_[5 * 16384], true);
						set_memory(2, &ram_[6 * 16384], &ram_[6 * 16384], true);
						set_memory(3, &ram_[7 * 16384], &ram_[7 * 16384], true);
					break;

					case 0x04:
						set_memory(0, &ram_[4 * 16384], &ram_[4 * 16384], true);
						set_memory(1, &ram_[5 * 16384], &ram_[5 * 16384], true);
						set_memory(2, &ram_[6 * 16384], &ram_[6 * 16384], true);
						set_memory(3, &ram_[3 * 16384], &ram_[3 * 16384], false);
					break;

					case 0x06:
						set_memory(0, &ram_[4 * 16384], &ram_[4 * 16384], true);
						set_memory(1, &ram_[7 * 16384], &ram_[7 * 16384], true);
						set_memory(2, &ram_[6 * 16384], &ram_[6 * 16384], true);
						set_memory(3, &ram_[3 * 16384], &ram_[3 * 16384], false);
					break;
				}

				return;
			}

			// Apply standard 128kb-esque mapping (albeit with extra ROM to pick from).
			const auto rom = &rom_[ (((port1ffd_ >> 1) & 2) | ((port7ffd_ >> 4) & 1)) * 16384];
			set_memory(0, rom, nullptr, false);

			set_memory(1, &ram_[5 * 16384], &ram_[5 * 16384], true);
			set_memory(2, &ram_[2 * 16384], &ram_[2 * 16384], false);

			const auto high_ram = &ram_[(port7ffd_ & 7) * 16384];
			set_memory(3, high_ram, high_ram, (port7ffd_ & 7) >= 4);
		}

		void set_memory(int bank, const uint8_t *read, uint8_t *write, bool is_contended) {
			is_contended_[bank] = is_contended;
			read_pointers_[bank] = read - bank*16384;
			write_pointers_[bank] = (write ? write : scratch_.data()) - bank*16384;
		}

		// MARK: - Audio.
		Concurrency::DeferringAsyncTaskQueue audio_queue_;
		GI::AY38910::AY38910<false> ay_;
		Audio::Toggle audio_toggle_;
		Outputs::Speaker::CompoundSource<GI::AY38910::AY38910<false>, Audio::Toggle> mixer_;
		Outputs::Speaker::LowpassSpeaker<Outputs::Speaker::CompoundSource<GI::AY38910::AY38910<false>, Audio::Toggle>> speaker_;

		HalfCycles time_since_audio_update_;
		void update_audio() {
			speaker_.run_for(audio_queue_, time_since_audio_update_.divide_cycles(Cycles(2)));
		}

		// MARK: - Video.
		static constexpr VideoTiming video_timing = VideoTiming::Plus3;
		JustInTimeActor<Video<video_timing>> video_;

		// MARK: - Keyboard.
		Sinclair::ZX::Keyboard::Keyboard keyboard_;
		Sinclair::ZX::Keyboard::KeyboardMapper keyboard_mapper_;

		// MARK: - Tape and disc.
		Storage::Tape::BinaryTapePlayer tape_player_;

		bool use_automatic_tape_motor_control_ = true;
		HalfCycles cycles_since_tape_input_read_;
		int recent_tape_hits_ = 0;

		bool allow_fast_tape_hack_ = false;
		void set_use_fast_tape() {
			// TODO.
		}
};


}
}

using namespace Sinclair::ZXSpectrum;

Machine *Machine::ZXSpectrum(const Analyser::Static::Target *target, const ROMMachine::ROMFetcher &rom_fetcher) {
	const auto zx_target = dynamic_cast<const Analyser::Static::ZXSpectrum::Target *>(target);

	switch(zx_target->model) {
		case Model::Plus2a:	return new ConcreteMachine<Model::Plus2a>(*zx_target, rom_fetcher);
		case Model::Plus3:	return new ConcreteMachine<Model::Plus3>(*zx_target, rom_fetcher);
	}

	return nullptr;
}

Machine::~Machine() {}
