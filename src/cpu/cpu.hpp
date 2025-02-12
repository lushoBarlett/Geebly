#pragma once

#include <iostream>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <atomic>
#include <array>

#include "../aliases.hpp"
#include "../bus.hpp"
#include "../log.hpp"
#include "../devices/clock.hpp"
#include "../utils.hpp"

#include "registers.hpp"
#include "ops.hpp"

#ifdef __linux__
#include <sys/unistd.h>
#define GEEBLY_PERF_FFS __builtin_ffs
#define GEEBLY_PERF_SLEEP usleep(1);
#endif

// Windows only:
// Undef and redef "N", causes macro conflicts inside windows.h (sigh)
#ifdef _WIN32
//#undef N
//    #define WIN32_LEAN_AND_MEAN
//    #include <windows.h>
//#define N 0b01000000
#define GEEBLY_PERF_FFS ffs
#define GEEBLY_PERF_SLEEP // Sleep(0);
#endif


namespace gameboy {
    namespace cpu {
        #ifdef _WIN32
        template <class T> size_t ffs(T n) {
            for (int c = 0; c < sizeof(T)*8; c++) {
                if (n & (1 << c)) return c + 1;
            }
            return 0;
        }
        #endif

        void init() {
            using namespace registers;

            pc = 0x0;
            sp = 0x0;
            af = 0x0;
            bc = 0x0;
            de = 0x0;
            hl = 0x0;

            if (settings::skip_bootrom) {
                pc = 0x0100;
                sp = 0xfffe;
                af = settings::cgb_mode ? 0x11b0 : 0x01b0;
                bc = 0x0013;
                de = 0x00d8;
                hl = 0x014d;
            }
        }
        
        bool halt_bug = false,
             halt_ime_state = false,
             ei_issued = false;
        int  ei_delay = 0;

        u8 fired = 0;

        inline void handle_interrupts() {
            using namespace registers;

            if (ei_issued && !(ei_delay--)) {
                ime = true;
                ei_issued = false;
            }
            
            u8& ie = bus::ref(0xffff),
              & ia = bus::ref(0xff0f);


            u8 i = GEEBLY_PERF_FFS(ie & ia & 0x1f);

            if (!i) return;

            if (ime) {
                ime = false;
                if (halted) registers::last_instruction_cycles = 4;
                ia &= ~(1 << (i-1));
                
                call(0x40 + (0x8 * (i - 1)));
            }

            halted = false;
            //stopped = false;
        }

        inline void enable_interrupts() {
            ei_issued = true;
            ei_delay = 1;
        }

        inline void update(size_t pci, size_t ci, bool j = false) {
            registers::cycles += ci;
            registers::last_instruction_cycles = ci;
            s.pc_increment = pci;
            jump = j;
        }

        void fetch() {
            s.opcode = bus::read(registers::pc, 1);
            s.imm = bus::read(registers::pc+1, 2);
            s.imm8 = utils::low8(s.imm);
        }

        bool execute(u8 override = 0x0) {
            using namespace registers;

            u8 opcode = override ? override : s.opcode;

            //if (settings::debugger_enabled) {
            //    if (!run) { while (step) { GEEBLY_PERF_SLEEP } }
            //}

            if (halted || stopped) {
                update(0, 4);
                goto skip;
            }
            
            switch (opcode) {
                // nop
                case 0x00: { update(1, 4); } break;

                // inc %r8;
                case 0x04: case 0x14: case 0x24: case 0x0c: case 0x1c: case 0x2c: case 0x3c:
                case 0x05: case 0x15: case 0x25: case 0x0d: case 0x1d: case 0x2d: case 0x3d: {
                    op_idc(r[utils::mid3(opcode)], utils::bit(0, opcode));
                    update(1, 4);
                } break;

                // stop #i8
                case 0x10: {
                    if (!clock::do_switch()) {
                        //stopped = true;
                    }
                    update(2, 4);
                } break;

                // inc *%hl, dec *%hl;
                case 0x34: case 0x35: {
                    u8 temp = bus::read(hl, 1);
                    op_idc(temp, utils::bit(0, opcode));
                    bus::write(hl, temp, 1);
                    update(1, 12);
                } break;

                // ld r8, r8
                case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x47:
                case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4f:
                case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x57:
                case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5f:
                case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x67:
                case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6f:
                case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7f: {
                    r[utils::mid3(opcode)] = r[utils::low3(opcode)];
                    update(1, 4);
                } break;

                // rla; rlca;
                case 0x07: case 0x17: {
                    op_rlc(r[a], utils::bit(4, opcode));
                    update(1, 4);
                } break;

                // rra; rrca;
                case 0x0f: case 0x1f: {
                    op_rrc(r[a], utils::bit(4, opcode));
                    update(1, 4);
                } break;

                // halt
                case 0x76: {
                    u8 ie = bus::read(0xffff, 1), ia = bus::read(0xff0f, 1);
                    halted = ime || (ia & ie & 0x1f) == 0;
                    update(1, 4);
                } break;

                // ld r8, (hl)
                case 0x46: case 0x4e: case 0x56: case 0x5e: case 0x66: case 0x6e: case 0x7e: {
                    r[utils::mid3(opcode)] = bus::read(hl, 1);
                    update(1, 8);
                    break;
                } break;

                // ld (hl), r8
                case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x77: {
                    bus::write(hl, r[utils::low3(opcode)], 1);
                    update(1, 8);
                } break;

                // add a, r8; adc a, r8
                case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x87:   
                case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8c: case 0x8d: case 0x8f: {
                    bool carry = utils::bit(3, opcode) && get_carry();
                    op_adc(r[a], r[utils::low3(opcode)], carry);
                    update(1, 4);
                } break;

                // add a, *%hl; adc a, *%hl
                case 0x86: case 0x8e: {
                    bool carry = utils::bit(3, opcode) && get_carry();
                    op_adc(r[a], bus::read((u16)hl, 1), carry);
                    update(1, 8);
                } break;

                // sub a, r8; sbc a, r8
                case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x97:   
                case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9f: {
                    bool carry = utils::bit(3, opcode) && get_carry();
                    op_sbc(r[a], r[utils::low3(opcode)], carry);
                    update(1, 4);
                } break;

                case 0x96: case 0x9e: {
                    bool carry = utils::bit(3, opcode) && get_carry();
                    op_sbc(r[a], bus::read((u16)hl, 1), carry);
                    update(1, 8);
                } break;

                // and a, r8; and a, (hl)
                case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xa7: {
                    op_and(r[a], r[utils::low3(opcode)]);
                    update(1, 4);
                } break;

                case 0xa6: { op_and(r[a], bus::read(hl, 1)); update(1, 8); } break;

                // or a, r8; or a, (hl)
                case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb7: {
                    op_or(r[a], r[utils::low3(opcode)]);
                    update(1, 4);
                } break;

                case 0xb6: { op_or(r[a], bus::read(hl, 1)); update(1, 8); } break;

                // xor a, r8; xor a, (hl)
                case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xaf: {
                    op_xor(r[a], r[utils::low3(opcode)]);
                    update(1, 4);
                } break;

                case 0xae: { op_xor(r[a], bus::read(hl, 1)); update(1, 8); } break;

                // cp a, r8; cp a, (hl)
                case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbf: {
                    op_cp(r[a], r[utils::low3(opcode)]);
                    update(1, 4);
                } break;

                case 0xbe: { op_cp(r[a], bus::read(hl, 1)); update(1, 8); } break;

                case 0xc6: case 0xce: {
                    bool carry = utils::bit(4, opcode) && get_carry();
                    op_adc(r[a], s.imm8, carry);
                    update(2, 8);
                } break;

                case 0xd6: case 0xde: {
                    bool carry = utils::bit(4, opcode) && get_carry();
                    op_sbc(r[a], s.imm8, carry);
                    update(2, 8);
                } break;

                case 0xe6: { op_and(r[a], s.imm8); update(2, 8); } break;
                case 0xee: { op_xor(r[a], s.imm8); update(2, 8); } break;
                case 0xf6: { op_or(r[a], s.imm8); update(2, 8); } break;
                case 0xfe: { op_cp(r[a], s.imm8); update(2, 8); } break;

                // ld r8, d8; ld *%hl, d8;
                case 0x06: case 0x16: case 0x26: case 0x0e: case 0x1e: case 0x2e: case 0x3e: {
                    r[utils::mid3(opcode)] = s.imm8;
                    update(2, 8);
                } break;
                
                case 0x36: { bus::write(hl, s.imm8, 1); update(2, 12); } break;

                // rst #rst_vector;
                case 0xc7: case 0xd7: case 0xe7: case 0xf7: case 0xcf: case 0xdf: case 0xef: case 0xff: {
                    push(pc+1);
                    pc = utils::bits(3, 5, opcode) << 3;
                    update(1, 16, true);
                } break;

                // ld %r16, #imm16;
                case 0x01: { bc = s.imm; update(3, 12); } break;
                case 0x11: { de = s.imm; update(3, 12); } break;
                case 0x21: { hl = s.imm; update(3, 12); } break;
                case 0x31: { sp = s.imm; update(3, 12); } break;

                case 0x08: { bus::write(s.imm, sp, 2); update(3, 20); } break;
                
                // inc %r16;
                case 0x03: { bc++; update(1, 8); } break;
                case 0x13: { de++; update(1, 8); } break;
                case 0x23: { hl++; update(1, 8); } break;
                case 0x33: { sp++; update(1, 8); } break;

                // dec %r16;
                case 0x0b: { bc--; update(1, 8); } break;
                case 0x1b: { de--; update(1, 8); } break;
                case 0x2b: { hl--; update(1, 8); } break;
                case 0x3b: { sp--; update(1, 8); } break;

                // ld *%r16, %a;
                case 0x02: { bus::write((u16)bc, r[a], 1); update(1, 8); } break;
                case 0x12: { bus::write((u16)de, r[a], 1); update(1, 8); } break;

                // ld *%hl+-, %a;
                case 0x22: { bus::write(hl++, r[a], 1); update(1, 8); } break;
                case 0x32: { bus::write(hl--, r[a], 1); update(1, 8); } break;

                // ld *%r16, %a;
                case 0x0a: { r[a] = bus::read((u16)bc, 1); update(1, 8); } break;
                case 0x1a: { r[a] = bus::read((u16)de, 1); update(1, 8); } break;

                // ld %a, *%hl+-;
                case 0x2a: { r[a] = bus::read(hl++, 1); update(1, 8); } break;
                case 0x3a: { r[a] = bus::read(hl--, 1); update(1, 8); } break;

                // add %hl, %r16;
                case 0x09: { op_addhl((u16)bc); update(1, 8); } break;
                case 0x19: { op_addhl((u16)de); update(1, 8); } break;
                case 0x29: { op_addhl((u16)hl); update(1, 8); } break;
                case 0x39: { op_addhl(sp); update(1, 8); } break;

                case 0x20: { if (!test_flags(Z)) { pc += 2; pc += (s8)s.imm8; update(2, 12, true); } else { update(2, 8); } } break;
                case 0x30: { if (!test_flags(C)) { pc += 2; pc += (s8)s.imm8; update(2, 12, true); } else { update(2, 8); } } break;
                case 0x18: { pc += 2; pc += (s8)s.imm8; update(2, 12, true); } break;
                case 0x28: { if ( test_flags(Z)) { pc += 2; pc += (s8)s.imm8; update(2, 12, true); } else { update(2, 8); } } break;
                case 0x38: { if ( test_flags(C)) { pc += 2; pc += (s8)s.imm8; update(2, 12, true); } else { update(2, 8); } } break;
                
                case 0xc1: { bc = pop(); update(1, 12); } break;
                case 0xd1: { de = pop(); update(1, 12); } break;
                case 0xe1: { hl = pop(); update(1, 12); } break;
                case 0xf1: { af = pop() & 0xfff0; update(1, 12); } break;
                
                case 0xc5: { push((u16)bc); update(1, 12); } break;
                case 0xd5: { push((u16)de); update(1, 12); } break;
                case 0xe5: { push((u16)hl); update(1, 12); } break;
                case 0xf5: { push((u16)af); update(1, 12); } break;

                case 0xc0: { if (!test_flags(Z)) { pc = pop(); update(1, 20, true); } else { update(1, 8); } } break;
                case 0xd0: { if (!test_flags(C)) { pc = pop(); update(1, 20, true); } else { update(1, 8); } } break;
                case 0xc8: { if ( test_flags(Z)) { pc = pop(); update(1, 20, true); } else { update(1, 8); } } break;
                case 0xd8: { if ( test_flags(C)) { pc = pop(); update(1, 20, true); } else { update(1, 8); } } break;
                case 0xc9: { pc = pop(); update(1, 16, true); } break;
                
                case 0xc2: { if (!test_flags(Z)) { pc = s.imm; update(3, 16, true); } else { update(3, 12); } } break;
                case 0xd2: { if (!test_flags(C)) { pc = s.imm; update(3, 16, true); } else { update(3, 12); } } break;
                case 0xca: { if ( test_flags(Z)) { pc = s.imm; update(3, 16, true); } else { update(3, 12); } } break;
                case 0xda: { if ( test_flags(C)) { pc = s.imm; update(3, 16, true); } else { update(3, 12); } } break;
                case 0xc3: { pc = s.imm; update(3, 16, true); } break;

                case 0xf8: {
                    u16 res = sp + (s8)s.imm8;
                    hl = res;
                    set_flags(Z | N, false);
                    set_flags(C, utils::low8(res) < s.imm8);
                    set_flags(H, utils::low4(res) < utils::low4(s.imm8));
                    update(2, 12);
                } break;

                case 0xf9: { sp = (u16)hl; update(1, 8); } break;

                // ld *#i16, %a;
                case 0xea: { bus::write(s.imm, r[a], 1); update(3, 16); } break;

                // ld %a, *#i16;
                case 0xfa: { r[a] = bus::read(s.imm, 1); update(3, 16); } break;

                // add %sp, #si8;
                // TO-DO implement carry and halfcarry detection
                case 0xe8: {
                    sp += (s8)s.imm8;
                    set_flags(Z | N, false);
                    set_flags(C, utils::low8(sp) < s.imm8);
                    set_flags(H, utils::low4(sp) < utils::low4(s.imm8));
                    update(2, 16);
                } break;

                // jp %hl;
                case 0xe9: { pc = (u16)hl; update(1, 4, true); } break;

                // call[z,c,nz,nc] #i16;
                case 0xc4: { if (!test_flags(Z)) { push(pc+3); pc = s.imm; update(3, 24, true); } else { update(3, 12); } } break;
                case 0xd4: { if (!test_flags(C)) { push(pc+3); pc = s.imm; update(3, 24, true); } else { update(3, 12); } } break;
                case 0xcc: { if ( test_flags(Z)) { push(pc+3); pc = s.imm; update(3, 24, true); } else { update(3, 12); } } break;
                case 0xdc: { if ( test_flags(C)) { push(pc+3); pc = s.imm; update(3, 24, true); } else { update(3, 12); } } break;
                
                // call #i16;
                case 0xcd: { push(pc+3); pc = s.imm; update(3, 24, true); } break;

                // ld *#0xff00+#i8, %a; ld %a, *#0xff00+#i8;
                case 0xe0: { bus::write(0xff00 | s.imm8, r[a], 1); update(2, 12); } break;
                case 0xf0: { r[a] = bus::read(0xff00 | s.imm8, 1); update(2, 12); } break;

                // ld *#0xff00+%c, a, ld a, *#0xff00+%c;
                case 0xe2: { bus::write(0xff00 | r[c], r[a], 1); update(1, 8); } break;
                case 0xf2: { r[a] = bus::read(0xff00 | r[c], 1); update(1, 8); } break;

                // ei; di;
                case 0xfb: { enable_interrupts(); update(1, 4); } break;
                case 0xf3: { ime = false; update(1, 4); } break;

                // reti
                case 0xd9: { pc = pop(); ime = true; update(1, 16, true); } break;

                // cpl
                case 0x2f: { r[a] = ~r[a]; set_flags(N | H, true); update(1, 4); } break;

                // daa
                case 0x27: { op_daa(r[a]); update(1, 4); } break;

                case 0xcb: {
                    opcode = bus::read(++registers::pc, 1);

                    switch (opcode) {
                        // bit b, r
                        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x47:
                        case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4f:
                        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x57:
                        case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5f:
                        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x67:
                        case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6f:
                        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x77:
                        case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7f: {
                            op_bit(r[utils::low3(opcode)], utils::mid3(opcode));
                            update(1, 8);
                        } break;

                        case 0x46: case 0x4e:
                        case 0x56: case 0x5e:
                        case 0x66: case 0x6e:
                        case 0x76: case 0x7e: {
                            u8 temp = bus::read(hl, 1);
                            op_bit(temp, utils::mid3(opcode));
                            bus::write(hl, temp, 1);
                            update(1, 16);
                        } break;

                        // res b, r; set b, r
                        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x87:
                        case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8c: case 0x8d: case 0x8f:
                        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x97:
                        case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9f:
                        case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xa7:
                        case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xaf:
                        case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb7:
                        case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbf:
                        case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5: case 0xc7:
                        case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd: case 0xcf:
                        case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd7:
                        case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xdf:
                        case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5: case 0xe7:
                        case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xef:
                        case 0xf0: case 0xf1: case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf7:
                        case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xff: {
                            op_rst(r[utils::low3(opcode)], utils::mid3(opcode), utils::bit(6, opcode));
                            update(1, 8);
                        } break;

                        case 0x86: case 0x8e: case 0x96: case 0x9e:
                        case 0xa6: case 0xae: case 0xb6: case 0xbe:
                        case 0xc6: case 0xce: case 0xd6: case 0xde:
                        case 0xe6: case 0xee: case 0xf6: case 0xfe: {
                            u8 temp = bus::read(hl, 1);
                            op_rst(temp, utils::mid3(opcode), utils::bit(6, opcode));
                            bus::write(hl, temp, 1);
                            update(1, 16);
                        } break;

                        case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x07:
                        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x17: {
                            op_rlc(r[utils::low3(opcode)], utils::bit(4, opcode), true);
                            update(1, 8);
                        } break;

                        case 0x06: case 0x16: {
                            u8 temp = bus::read(hl, 1);
                            op_rlc(temp, utils::bit(4, opcode), true);
                            bus::write(hl, temp, 1);
                            update(1, 16);
                        } break;
                        
                        case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0f:
                        case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1f: {
                            op_rrc(r[utils::low3(opcode)], utils::bit(4, opcode), true);
                            update(1, 8);
                        } break;

                        case 0x0e: case 0x1e: {
                            u8 temp = bus::read(hl, 1);
                            op_rrc(temp, utils::bit(4, opcode), true);
                            bus::write(hl, temp, 1);
                            update(1, 16);
                        } break;

                        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x27: {
                            op_sla(r[utils::low3(opcode)]);
                            update(1, 8);
                        } break;

                        case 0x26: {
                            u8 temp = bus::read(hl, 1);
                            op_sla(temp);
                            bus::write(hl, temp, 1);
                            update(1, 16);
                        } break;

                        case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2f:
                        case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3f: {
                            op_sra(r[utils::low3(opcode)], utils::bit(4, opcode));
                            update(1, 8);
                        } break;

                        case 0x2e: case 0x3e: {
                            u8 temp = bus::read(hl, 1);
                            op_sra(temp, utils::bit(4, opcode));
                            bus::write(hl, temp, 1);
                            update(1, 16);
                        } break;

                        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x37: {
                            op_swap(r[utils::low3(opcode)]);
                            update(1, 8);
                        } break;

                        case 0x36: {
                            u8 temp = bus::read(hl, 1);
                            op_swap(temp);
                            bus::write(hl, temp, 1);
                            update(1, 16);
                        } break;
                    }
                } break;

                // scf; ccf
                case 0x37: { set_flags(C, true); set_flags(N | H, false); update(1, 4); }; break;
                case 0x3f: { set_flags(C, !test_flags(C)); set_flags(N | H, false); update(1, 4); }; break;

                default: {
                    //_log(error, "Unimplemented opcode 0x%02x @ pc=%04x", opcode, pc);
                    update(1, 4);
                    //return false; // Halt and Catch Fire!
                }
            }

            if (!jump) { if (!halt_bug) { pc += s.pc_increment; } else { halt_bug = false; } }

            skip:

            if (settings::debugger_enabled && !cpu::run) step = true;

            jump = false;
            done = true;

            return true;
        }

        bool cycle() {
            handle_interrupts();
            fetch();
            return execute();
        }
    };
}