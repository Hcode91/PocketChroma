// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff, Manuel Abadia, Couriersud, David Haywood

/*****************************************************************************

    AXC51-CORE / AX208 SoC (AppoTech Inc.)

    AXC51CORE:
    somes sources indicate that the extended opcode encoding may change
    on some CPU models despite all being 'AXC51CORE' however we lack solid
    information on this at present.

    AX208:
    The CPU has 0x2000 bytes of internal ROM mapped at 0x8000-0x9fff providing
    bootcode, operating kernel and many standard library functions

 *****************************************************************************/

#include "axc51.h"

#define LOG_UNSORTED       (1U << 1)
#define LOG_PORTS          (1U << 2)
#define LOG_UNHANDLED      (1U << 3)
#define LOG_UNHANDLED_XSFR (1U << 4)



#define VERBOSE     (0)






/***************************************************************************
    CONSTANTS
***************************************************************************/



axc51base_cpu_device::axc51base_cpu_device()
	: m_pc(0)
	, m_features(0)
	, m_rom_size(0)
	, m_num_interrupts(5)
	, m_rtemp(0)
{
	for (int i = 0; i < 0x80; i++)
	{
		m_sfr_regs[i] = 0x00;
		m_xsfr_regs[i] = 0x00;
	}
	for (auto &d : m_port_in_cb) d.def = 0xff;
	m_spi_in_cb.def = 0xff;
	memset(m_scratchpad, 0, sizeof(m_scratchpad));
	memset(m_mainram, 0, sizeof(m_mainram));
	memset(m_jpegram, 0, sizeof(m_jpegram));
	memset(m_rom, 0xff, sizeof(m_rom));

	m_uid[0] = 0x00; // not used?
	m_uid[1] = 0x00; // used in RTC / USB code?
	m_uid[2] = 0x91; // used in crypt code?
	m_uid[3] = 0xb5;
}

/* address spaces (replaces MAME address maps) */
uint8_t axc51base_cpu_device::prog_read(offs_t a)
{
	a &= 0xffff;
	if (a >= 0x4000 && a < 0x7000) return m_mainram[a - 0x4000];
	if (a >= 0x8000 && a < 0xa000) return m_rom[a - 0x8000];
	return 0x00;
}
uint8_t axc51base_cpu_device::data_read(offs_t a)
{
	a &= 0x7ff;
	return (a < 0x400) ? m_scratchpad[a] : 0x00;
}
void axc51base_cpu_device::data_write(offs_t a, uint8_t d)
{
	a &= 0x7ff;
	if (a < 0x400) m_scratchpad[a] = d;
}
uint8_t axc51base_cpu_device::io_read(offs_t a)
{
	a &= 0x1ffff;
	if (a < 0x400) return m_scratchpad[a];
	if (a >= 0x3000 && a < 0x4000) return xsfr_read(a - 0x3000);
	if (a >= 0x4000 && a < 0x7000) return m_mainram[a - 0x4000];
	if (a >= 0x7000 && a < 0x7800) return m_jpegram[a - 0x7000];
	return 0x00;
}
void axc51base_cpu_device::io_write(offs_t a, uint8_t d)
{
	a &= 0x1ffff;
	if (a < 0x400) m_scratchpad[a] = d;
	else if (a >= 0x3000 && a < 0x4000) xsfr_write(a - 0x3000, d);
	else if (a >= 0x4000 && a < 0x7000) m_mainram[a - 0x4000] = d;
	else if (a >= 0x7000 && a < 0x7800) m_jpegram[a - 0x7000] = d;
}

/* Read/Write a byte from/to the Internal RAM indirectly */
/* (called from indirect addressing)                     */
/* these go through DBASE register on axc51 (at least stack accesses) */
uint8_t axc51base_cpu_device::iram_indirect_read(offs_t a) { return data_read((m_sfr_regs[SFR_DBASE] * 4) + a); }
void axc51base_cpu_device::iram_indirect_write(offs_t a, uint8_t d) { data_write((m_sfr_regs[SFR_DBASE] * 4) + a, d); }

/***************************************************************************
    SHORTCUTS
***************************************************************************/

/* SFR Registers - These are accessed directly for speed on read */
/* Read accessors                                                */

#define SET_SFR_A(a,v)  do { m_sfr_regs[a] = (v); } while (0)

#define ACC         m_sfr_regs[SFR_ACC]
#define PSW         m_sfr_regs[SFR_PSW]

#define P0          ((const uint8_t) m_sfr_regs[SFR_P0])
#define P1          ((const uint8_t) m_sfr_regs[SFR_P1])
#define P2          ((const uint8_t) m_sfr_regs[SFR_P2])
#define P3          ((const uint8_t) m_sfr_regs[SFR_P3])
#define P4          ((const uint8_t) m_sfr_regs[SFR_P4])

#define SP          m_sfr_regs[SFR_SP]
#define DPL0        m_sfr_regs[SFR_DPL0]
#define DPH0        m_sfr_regs[SFR_DPH0]
#define PCON        m_sfr_regs[SFR_PCON]
#define IE          m_sfr_regs[SFR_IE]
#define IE1         m_sfr_regs[SFR_IE1]
#define IP          m_sfr_regs[SFR_IP]
#define B           m_sfr_regs[SFR_B]
#define ER8         m_sfr_regs[SFR_ER8]

#define DPL1        m_sfr_regs[SFR_DPL1]
#define DPH1        m_sfr_regs[SFR_DPH1]


#define ER00        m_sfr_regs[SFR_ER00]
#define ER01        m_sfr_regs[SFR_ER01]

#define ER10        m_sfr_regs[SFR_ER10]
#define ER11        m_sfr_regs[SFR_ER11]

#define ER20        m_sfr_regs[SFR_ER20]
#define ER21        m_sfr_regs[SFR_ER21]

#define ER30        m_sfr_regs[SFR_ER30]
#define ER31        m_sfr_regs[SFR_ER31]

#define GP0         m_sfr_regs[SFR_GP0]
#define GP1         m_sfr_regs[SFR_GP1]
#define GP2         m_sfr_regs[SFR_GP2]
#define GP3         m_sfr_regs[SFR_GP3]
#define GP4         m_sfr_regs[SFR_GP4]
#define GP5         m_sfr_regs[SFR_GP5]
#define GP6         m_sfr_regs[SFR_GP6]
#define GP7         m_sfr_regs[SFR_GP7]

#define R_REG(r)    m_scratchpad[(r) | (PSW & 0x18)]

#define DPTR0       ((DPH0<<8) | DPL0)
#define DPTR1       ((DPH1<<8) | DPL1)

#define ER0         ((ER01<<8) | ER00)
#define ER1         ((ER11<<8) | ER10)
#define ER2         ((ER21<<8) | ER20)
#define ER3         ((ER31<<8) | ER30)

#define SET_PSW(v)  do { m_sfr_regs[SFR_PSW] = (v); SET_PARITY(); } while (0)
#define SET_ACC(v)  do { m_sfr_regs[SFR_ACC] = (v); SET_PARITY(); } while (0)

/* These trigger actions on modification and have to be written through SFR_W */
#define SET_P0(v)   iram_write(SFR_P0, v)
#define SET_P1(v)   iram_write(SFR_P1, v)
#define SET_P2(v)   iram_write(SFR_P2, v)
#define SET_P3(v)   iram_write(SFR_P3, v)

/* No actions triggered on write */
#define SET_REG(r, v)   do { m_scratchpad[(r) | (PSW & 0x18)] = (v); } while (0)

#define SET_DPTR0(n)    do { DPH0 = ((n) >> 8) & 0xff; DPL0 = (n) & 0xff; } while (0)

#define SET_DPTR1(n)    do { DPH1 = ((n) >> 8) & 0xff; DPL1 = (n) & 0xff; } while (0)

#define SET_ER0(n)      do { ER01 = ((n) >> 8) & 0xff; ER00 = (n) & 0xff; } while (0)
#define SET_ER1(n)      do { ER11 = ((n) >> 8) & 0xff; ER10 = (n) & 0xff; } while (0)
#define SET_ER2(n)      do { ER21 = ((n) >> 8) & 0xff; ER20 = (n) & 0xff; } while (0)
#define SET_ER3(n)      do { ER31 = ((n) >> 8) & 0xff; ER30 = (n) & 0xff; } while (0)

#define SET_ER8(n)      do { ER8 = (n);} while (0)

#define SET_GP0(n)      do { GP0 = (n);} while (0)
#define SET_GP1(n)      do { GP1 = (n);} while (0)
#define SET_GP2(n)      do { GP2 = (n);} while (0)
#define SET_GP3(n)      do { GP3 = (n);} while (0)
#define SET_GP4(n)      do { GP4 = (n);} while (0)
#define SET_GP5(n)      do { GP5 = (n);} while (0)
#define SET_GP6(n)      do { GP6 = (n);} while (0)
#define SET_GP7(n)      do { GP7 = (n);} while (0)

/* Macros for Setting Flags */
#define SET_X(R, v) do { R = (v);} while (0)

#define SET_CY(n)       SET_PSW((PSW & 0x7f) | (n<<7))  //Carry Flag
#define SET_AC(n)       SET_PSW((PSW & 0xbf) | (n<<6))  //Aux.Carry Flag
#define SET_EC(n)       SET_PSW((PSW & 0xdf) | (n<<5))  //Extended Instruction Carry Flag EC (not FO)
#define SET_RS(n)       SET_PSW((PSW & 0xe7) | (n<<3))  //R Bank Select
#define SET_OV(n)       SET_PSW((PSW & 0xfb) | (n<<2))  //Overflow Flag
#define SET_EZ(n)       SET_PSW((PSW & 0xfd) | (n<<1))  //Extended Instruction Zero Flag EZ
#define SET_P(n)        SET_PSW((PSW & 0xfe) | (n<<0))  //Parity Flag

#define SET_BIT(R, n, v) do { R = (R & ~(1<<(n))) | ((v) << (n));} while (0)
#define GET_BIT(R, n) (((R)>>(n)) & 0x01)

/* Macros for accessing flags */

#define GET_CY          GET_BIT(PSW, 7)
#define GET_AC          GET_BIT(PSW, 6)
#define GET_EC          GET_BIT(PSW, 5) //Extended Instruction Carry Flag EC (not FO)
#define GET_RS          GET_BIT(PSW, 3)
#define GET_OV          GET_BIT(PSW, 2)
#define GET_EZ          GET_BIT(PSW, 1) //Extended Instruction Zero Flag EZ
#define GET_P           GET_BIT(PSW, 0)

#define GET_DMAIRQEN    GET_BIT(IE1, 6)

#define GET_EA          GET_BIT(IE, 7)
#define GET_SDCIRQEN    GET_BIT(IE, 6)
#define GET_SPIIRQEN    GET_BIT(IE, 5)
#define GET_USBIRQEN    GET_BIT(IE, 4)
#define GET_T3IRQEN     GET_BIT(IE, 3)
#define GET_T2IRQEN     GET_BIT(IE, 2)
#define GET_T1IRQEN     GET_BIT(IE, 1)
#define GET_T0IRQEN     GET_BIT(IE, 0)

#define SET_PARITY()    do {m_recalc_parity |= 1;} while (0)

/***************************************************************************
    INLINE FUNCTIONS
***************************************************************************/

void axc51base_cpu_device::clear_current_irq()
{
	LOGMASKED(LOG_UNHANDLED,"clear irq\n");
}

uint8_t axc51base_cpu_device::r_acc() { return m_sfr_regs[SFR_ACC]; }

uint8_t axc51base_cpu_device::r_psw() { return m_sfr_regs[SFR_PSW]; }


offs_t axc51base_cpu_device::external_ram_iaddr(offs_t offset, offs_t mem_mask)
{
	if (mem_mask == 0x00ff)
		return (offset & mem_mask) | 0x000;

	return offset;
}

/* Internal ram read/write */

uint8_t axc51base_cpu_device::iram_read(size_t offset)
{
	return (((offset) < 0x80) ? data_read(offset) : sfr_read(offset & 0x7f));
}

void axc51base_cpu_device::iram_write(size_t offset, uint8_t data)
{
	if ((offset) < 0x80)
		data_write(offset, data);
	else
		sfr_write(offset & 0x7f, data);
}

/*Push the current PC to the stack*/
void axc51base_cpu_device::push_pc()
{
	uint8_t tmpSP = SP+1;                     //Grab and Increment Stack Pointer
	iram_indirect_write(tmpSP, (m_pc & 0xff));                //Store low byte of PC to Internal Ram (Use iram_indirect_write to store stack above 128 bytes)
	tmpSP++;                                    // ""
	SP = tmpSP;                             // ""
	iram_indirect_write(tmpSP, ( (m_pc & 0xff00) >> 8));      //Store hi byte of PC to next address in Internal Ram (Use iram_indirect_write to store stack above 128 bytes)
}

/*Pop the current PC off the stack and into the pc*/
void axc51base_cpu_device::pop_pc()
{
	uint8_t tmpSP = SP;                           //Grab Stack Pointer
	m_pc = (iram_indirect_read(tmpSP--) & 0xff) << 8;        //Store hi byte to PC (must use iram_indirect_read to access stack pointing above 128 bytes)
	m_pc = m_pc | iram_indirect_read(tmpSP--);                 //Store lo byte to PC (must use iram_indirect_read to access stack pointing above 128 bytes)
	SP = tmpSP;                             //Decrement Stack Pointer
}

//Set the PSW Parity Flag
void axc51base_cpu_device::set_parity()
{
	//This flag will be set when the accumulator contains an odd # of bits set..
	uint8_t p = 0;
	int i;
	uint8_t a = ACC;

	for (i=0; i<8; i++) {       //Test for each of the 8 bits in the ACC!
		p ^= (a & 1);
		a = (a >> 1);
	}

	SET_P(p & 1);
}

uint8_t axc51base_cpu_device::bit_address_r(uint8_t offset)
{
	uint8_t   word;
	uint8_t   mask;
	int bit_pos;
	int distance;   /* distance between bit addressable words */
					/* 1 for normal bits, 8 for sfr bit addresses */

	m_last_bit = offset;

	//User defined bit addresses 0x20-0x2f (values are 0x0-0x7f)
	if (offset < 0x80) {
		distance = 1;
		word = ( (offset & 0x78) >> 3) * distance + 0x20;
		bit_pos = offset & 0x7;
		mask = (0x1 << bit_pos);
		return((iram_read(word) & mask) >> bit_pos);
	}
	//SFR bit addressable registers
	else {
		distance = 8;
		word = ( (offset & 0x78) >> 3) * distance + 0x80;
		bit_pos = offset & 0x7;
		mask = (0x1 << bit_pos);
		return ((iram_read(word) & mask) >> bit_pos);
	}
}


void axc51base_cpu_device::bit_address_w(uint8_t offset, uint8_t bit)
{
	int word;
	uint8_t   mask;
	int bit_pos;
	uint8_t   result;
	int distance;

	/* User defined bit addresses 0x20-0x2f (values are 0x0-0x7f) */
	if (offset < 0x80) {
		distance = 1;
		word = ((offset & 0x78) >> 3) * distance + 0x20;
		bit_pos = offset & 0x7;
		bit = (bit & 0x1) << bit_pos;
		mask = ~(1 << bit_pos) & 0xff;
		result = iram_read(word) & mask;
		result = result | bit;
		iram_write(word, result);
	}
	/* SFR bit addressable registers */
	else {
		distance = 8;
		word = ((offset & 0x78) >> 3) * distance + 0x80;
		bit_pos = offset & 0x7;
		bit = (bit & 0x1) << bit_pos;
		mask = ~(1 << bit_pos) & 0xff;
		result = iram_read(word) & mask;
		result = result | bit;
		iram_write(word, result);
	}
}

void axc51base_cpu_device::do_add_flags(uint8_t a, uint8_t data, uint8_t c)
{
	uint16_t result = a+data+c;
	int16_t result1 = (int8_t)a+(int8_t)data+c;

	SET_CY((result & 0x100) >> 8);
	result = (a&0x0f)+(data&0x0f)+c;
	SET_AC((result & 0x10) >> 4);
	SET_OV(result1 < -128 || result1 > 127);
}

void axc51base_cpu_device::do_sub_flags(uint8_t a, uint8_t data, uint8_t c)
{
	uint16_t result = a-(data+c);
	int16_t result1 = (int8_t)a-(int8_t)(data+c);

	SET_CY((result & 0x100) >> 8);
	result = (a&0x0f)-((data&0x0f)+c);
	SET_AC((result & 0x10) >> 4);
	SET_OV((result1 < -128 || result1 > 127));
}

uint32_t axc51base_cpu_device::get_dptr0_with_autoinc(uint8_t auto_inc)
{
	uint32_t addr = external_ram_iaddr(DPTR0, 0xffff);
	if (auto_inc) // auto-increment enabled
	{
		if (m_sfr_regs[SFR_DPCON] & 0x20) // DPID0  DPTR0 increase direction control
		{
			uint16_t dptr = (DPTR0)-1;
			SET_DPTR0(dptr);
		}
		else
		{
			uint16_t dptr = (DPTR0)+1;
			SET_DPTR0(dptr);
		}
	}
	return addr;
}

uint32_t axc51base_cpu_device::get_dptr1_with_autoinc(uint8_t auto_inc)
{
	uint32_t addr = external_ram_iaddr(DPTR1, 0xffff);
	if (auto_inc) // auto-increment enabled
	{
		if (m_sfr_regs[SFR_DPCON] & 0x10) // DPID1  DPTR1 increase direction control
		{
			uint16_t dptr = (DPTR1)-1;
			SET_DPTR1(dptr);
		}
		else
		{
			uint16_t dptr = (DPTR1)+1;
			SET_DPTR1(dptr);
		}
	}
	return addr;
}

uint32_t axc51base_cpu_device::process_dptr_access()
{
	uint8_t auto_inc = m_sfr_regs[SFR_DPCON] & 0x08;
	uint32_t addr = (m_sfr_regs[SFR_DPCON] & 0x01) ? get_dptr1_with_autoinc(auto_inc) : get_dptr0_with_autoinc(auto_inc);

	if (m_sfr_regs[SFR_DPCON] & 0x04)
	{
		// auto toggle DPR
		m_sfr_regs[SFR_DPCON] ^= 0x01;
	}

	return addr;
}


/***************************************************************************
    OPCODES
***************************************************************************/

#define OPHANDLER( _name ) void axc51base_cpu_device::_name (uint8_t r)

#include "axc51ops.hxx"
#include "axc51extops.hxx"



void axc51base_cpu_device::execute_op(uint8_t op)
{
	if (m_recalc_parity)
	{
		set_parity();
		m_recalc_parity = 0;
	}

	m_last_op = op;

	switch( op )
	{
		case 0x00:  nop(op);                           break;  //NOP
		case 0x01:  ajmp(op);                      break;  //AJMP code addr
		case 0x02:  ljmp(op);                      break;  //LJMP code addr
		case 0x03:  rr_a(op);                      break;  //RR A
		case 0x04:  inc_a(op);                     break;  //INC A
		case 0x05:  inc_mem(op);                   break;  //INC data addr

		case 0x06:
		case 0x07:  inc_ir(op&1);                       break;  //INC @R0/@R1

		case 0x08:
		case 0x09:
		case 0x0a:
		case 0x0b:
		case 0x0c:
		case 0x0d:
		case 0x0e:
		case 0x0f:  inc_r(op&7);                       break;  //INC R0 to R7

		case 0x10:  jbc(op);                       break;  //JBC bit addr, code addr
		case 0x11:  acall(op);                     break;  //ACALL code addr
		case 0x12:  lcall(op);                         break;  //LCALL code addr
		case 0x13:  rrc_a(op);                     break;  //RRC A
		case 0x14:  dec_a(op);                     break;  //DEC A
		case 0x15:  dec_mem(op);                   break;  //DEC data addr

		case 0x16:
		case 0x17:  dec_ir(op&1);                  break;  //DEC @R0/@R1

		case 0x18:
		case 0x19:
		case 0x1a:
		case 0x1b:
		case 0x1c:
		case 0x1d:
		case 0x1e:
		case 0x1f:  dec_r(op&7);                   break;  //DEC R0 to R7

		case 0x20:  jb(op);                            break;  //JB  bit addr, code addr
		case 0x21:  ajmp(op);                      break;  //AJMP code addr
		case 0x22:  ret(op);                           break;  //RET
		case 0x23:  rl_a(op);                      break;  //RL A
		case 0x24:  add_a_byte(op);                    break;  //ADD A, #data
		case 0x25:  add_a_mem(op);                 break;  //ADD A, data addr

		case 0x26:
		case 0x27:  add_a_ir(op&1);                    break;  //ADD A, @R0/@R1

		case 0x28:
		case 0x29:
		case 0x2a:
		case 0x2b:
		case 0x2c:
		case 0x2d:
		case 0x2e:
		case 0x2f:  add_a_r(op&7);                 break;  //ADD A, R0 to R7

		case 0x30:  jnb(op);                           break;  //JNB bit addr, code addr
		case 0x31:  acall(op);                     break;  //ACALL code addr
		case 0x32:  reti(op);                      break;  //RETI
		case 0x33:  rlc_a(op);                     break;  //RLC A
		case 0x34:  addc_a_byte(op);                   break;  //ADDC A, #data
		case 0x35:  addc_a_mem(op);                    break;  //ADDC A, data addr

		case 0x36:
		case 0x37:  addc_a_ir(op&1);                   break;  //ADDC A, @R0/@R1

		case 0x38:
		case 0x39:
		case 0x3a:
		case 0x3b:
		case 0x3c:
		case 0x3d:
		case 0x3e:
		case 0x3f:  addc_a_r(op&7);                    break;  //ADDC A, R0 to R7

		case 0x40:  jc(op);                            break;  //JC code addr
		case 0x41:  ajmp(op);                      break;  //AJMP code addr
		case 0x42:  orl_mem_a(op);                 break;  //ORL data addr, A
		case 0x43:  orl_mem_byte(op);              break;  //ORL data addr, #data
		case 0x44:  orl_a_byte(op);                    break;
		case 0x45:  orl_a_mem(op);                 break;  //ORL A, data addr

		case 0x46:
		case 0x47:  orl_a_ir(op&1);                    break;  //ORL A, @RO/@R1

		case 0x48:
		case 0x49:
		case 0x4a:
		case 0x4b:
		case 0x4c:
		case 0x4d:
		case 0x4e:
		case 0x4f:  orl_a_r(op&7);                     break;  //ORL A, RO to R7

		case 0x50:  jnc(op);                       break;  //JNC code addr
		case 0x51:  acall(op);                     break;  //ACALL code addr
		case 0x52:  anl_mem_a(op);                 break;  //ANL data addr, A
		case 0x53:  anl_mem_byte(op);              break;  //ANL data addr, #data
		case 0x54:  anl_a_byte(op);                    break;  //ANL A, #data
		case 0x55:  anl_a_mem(op);                 break;  //ANL A, data addr

		case 0x56:
		case 0x57:  anl_a_ir(op&1);                    break;  //ANL A, @RO/@R1

		case 0x58:
		case 0x59:
		case 0x5a:
		case 0x5b:
		case 0x5c:
		case 0x5d:
		case 0x5e:
		case 0x5f:  anl_a_r(op&7);                 break;  //ANL A, RO to R7

		case 0x60:  jz(op);                            break;  //JZ code addr
		case 0x61:  ajmp(op);                      break;  //AJMP code addr
		case 0x62:  xrl_mem_a(op);                 break;  //XRL data addr, A
		case 0x63:  xrl_mem_byte(op);              break;  //XRL data addr, #data
		case 0x64:  xrl_a_byte(op);                    break;  //XRL A, #data
		case 0x65:  xrl_a_mem(op);                 break;  //XRL A, data addr

		case 0x66:
		case 0x67:  xrl_a_ir(op&1);                    break;  //XRL A, @R0/@R1

		case 0x68:
		case 0x69:
		case 0x6a:
		case 0x6b:
		case 0x6c:
		case 0x6d:
		case 0x6e:
		case 0x6f:  xrl_a_r(op&7);                 break;  //XRL A, R0 to R7

		case 0x70:  jnz(op);                           break;  //JNZ code addr
		case 0x71:  acall(op);                     break;  //ACALL code addr
		case 0x72:  orl_c_bitaddr(op);             break;  //ORL C, bit addr
		case 0x73:  jmp_iadptr(op);                    break;  //JMP @A+DPTR
		case 0x74:  mov_a_byte(op);                    break;  //MOV A, #data
		case 0x75:  mov_mem_byte(op);              break;  //MOV data addr, #data

		case 0x76:
		case 0x77:  mov_ir_byte(op&1);             break;  //MOV @R0/@R1, #data

		case 0x78:
		case 0x79:
		case 0x7a:
		case 0x7b:
		case 0x7c:
		case 0x7d:
		case 0x7e:
		case 0x7f:  mov_r_byte(op&7);              break;  //MOV R0 to R7, #data

		case 0x80:  sjmp(op);                      break;  //SJMP code addr
		case 0x81:  ajmp(op);                      break;  //AJMP code addr
		case 0x82:  anl_c_bitaddr(op);             break;  //ANL C, bit addr
		case 0x83:  movc_a_iapc(op);                   break;  //MOVC A, @A + PC
		case 0x84:  div_ab(op);                        break;  //DIV AB
		case 0x85:  mov_mem_mem(op);                   break;  //MOV data addr, data addr

		case 0x86:
		case 0x87:  mov_mem_ir(op&1);              break;  //MOV data addr, @R0/@R1

		case 0x88:
		case 0x89:
		case 0x8a:
		case 0x8b:
		case 0x8c:
		case 0x8d:
		case 0x8e:
		case 0x8f:  mov_mem_r(op&7);                   break;  //MOV data addr,R0 to R7

		case 0x90:  mov_dptr_byte(op);             break;  //MOV DPTR, #data
		case 0x91:  acall(op);                     break;  //ACALL code addr
		case 0x92:  mov_bitaddr_c(op);             break;    //MOV bit addr, C
		case 0x93:  movc_a_iadptr(op);             break;  //MOVC A, @A + DPTR
		case 0x94:  subb_a_byte(op);                   break;  //SUBB A, #data
		case 0x95:  subb_a_mem(op);                    break;  //SUBB A, data addr

		case 0x96:
		case 0x97:  subb_a_ir(op&1);                   break;  //SUBB A, @R0/@R1

		case 0x98:
		case 0x99:
		case 0x9a:
		case 0x9b:
		case 0x9c:
		case 0x9d:
		case 0x9e:
		case 0x9f:  subb_a_r(op&7);                    break;  //SUBB A, R0 to R7

		case 0xa0:  orl_c_nbitaddr(op);                break;  //ORL C, /bit addr
		case 0xa1:  ajmp(op);                      break;  //AJMP code addr
		case 0xa2:  mov_c_bitaddr(op);             break;  //MOV C, bit addr
		case 0xa3:  inc_dptr(op);                  break;  //INC DPTR
		case 0xa4:  mul_ab(op);                        break;  //MUL AB
		case 0xa5:  axc51_extended_a5(op);                       break;

		case 0xa6:
		case 0xa7:  mov_ir_mem(op&1);              break;  //MOV @R0/@R1, data addr

		case 0xa8:
		case 0xa9:
		case 0xaa:
		case 0xab:
		case 0xac:
		case 0xad:
		case 0xae:
		case 0xaf:  mov_r_mem(op&7);                   break;  //MOV R0 to R7, data addr

		case 0xb0:  anl_c_nbitaddr(op);                break;  //ANL C,/bit addr
		case 0xb1:  acall(op);                     break;  //ACALL code addr
		case 0xb2:  cpl_bitaddr(op);               break;  //CPL bit addr
		case 0xb3:  cpl_c(op);                     break;  //CPL C
		case 0xb4:  cjne_a_byte(op);                   break;  //CJNE A, #data, code addr
		case 0xb5:  cjne_a_mem(op);                    break;  //CJNE A, data addr, code addr

		case 0xb6:
		case 0xb7:  cjne_ir_byte(op&1);                break;  //CJNE @R0/@R1, #data, code addr

		case 0xb8:
		case 0xb9:
		case 0xba:
		case 0xbb:
		case 0xbc:
		case 0xbd:
		case 0xbe:
		case 0xbf:  cjne_r_byte(op&7);                 break;  //CJNE R0 to R7, #data, code addr

		case 0xc0:  push(op);                      break;  //PUSH data addr
		case 0xc1:  ajmp(op);                      break;  //AJMP code addr
		case 0xc2:  clr_bitaddr(op);               break;  //CLR bit addr
		case 0xc3:  clr_c(op);                         break;  //CLR C
		case 0xc4:  swap_a(op);                        break;  //SWAP A
		case 0xc5:  xch_a_mem(op);                 break;  //XCH A, data addr

		case 0xc6:
		case 0xc7:  xch_a_ir(op&1);                    break;  //XCH A, @RO/@R1

		case 0xc8:
		case 0xc9:
		case 0xca:
		case 0xcb:
		case 0xcc:
		case 0xcd:
		case 0xce:
		case 0xcf:  xch_a_r(op&7);                 break;  //XCH A, RO to R7

		case 0xd0:  pop(op);                           break;  //POP data addr
		case 0xd1:  acall(op);                     break;  //ACALL code addr
		case 0xd2:  setb_bitaddr(op);              break;  //SETB bit addr
		case 0xd3:  setb_c(op);                        break;  //SETB C
		case 0xd4:  da_a(op);                      break;  //DA A
		case 0xd5:  djnz_mem(op);                  break;  //DJNZ data addr, code addr

		case 0xd6:
		case 0xd7:  xchd_a_ir(op&1);                   break;  //XCHD A, @R0/@R1

		case 0xd8:
		case 0xd9:
		case 0xda:
		case 0xdb:
		case 0xdc:
		case 0xdd:
		case 0xde:
		case 0xdf:  djnz_r(op&7);                  break;  //DJNZ R0 to R7,code addr

		case 0xe0:  movx_a_idptr(op);              break;  //MOVX A,@DPTR
		case 0xe1:  ajmp(op);                      break;  //AJMP code addr

		case 0xe2:
		case 0xe3:  movx_a_ir(op&1);                   break;  //MOVX A, @R0/@R1

		case 0xe4:  clr_a(op);                     break;  //CLR A
		case 0xe5:  mov_a_mem(op);                 break;  //MOV A, data addr
		case 0xe6:
		case 0xe7:  mov_a_ir(op&1);                    break;  //MOV A,@RO/@R1

		case 0xe8:
		case 0xe9:
		case 0xea:
		case 0xeb:
		case 0xec:
		case 0xed:
		case 0xee:
		case 0xef:  mov_a_r(op&7);                 break;  //MOV A,R0 to R7

		case 0xf0:  movx_idptr_a(op);              break;  //MOVX @DPTR,A
		case 0xf1:  acall(op);                     break;  //ACALL code addr

		case 0xf2:
		case 0xf3:  movx_ir_a(op&1);                   break;  //MOVX @R0/@R1,A

		case 0xf4:  cpl_a(op);                     break;  //CPL A
		case 0xf5:  mov_mem_a(op);                 break;  //MOV data addr, A

		case 0xf6:
		case 0xf7:  mov_ir_a(op&1);                    break;  //MOV @R0/@R1, A

		case 0xf8:
		case 0xf9:
		case 0xfa:
		case 0xfb:
		case 0xfc:
		case 0xfd:
		case 0xfe:
		case 0xff:  mov_r_a(op&7);                 break;  //MOV R0 to R7, A
		default:
			illegal(op);
	}
}

/***************************************************************************
    OPCODE CYCLES
***************************************************************************/

/* # of oscilations each opcode requires*/
const uint8_t axc51base_cpu_device::axc51_cycles[256] = {
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

uint16_t axc51base_cpu_device::get_irq_base()
{
	int base = 0;

	switch (m_sfr_regs[SFR_DPCON] & 0xc0)
	{
	case 0x00:
	case 0xc0:
		base = 0; // invalid
		break;

	case 0x80:
		base = 0x8000;
		break;

	case 0x40:
		base = 0x4000;
		break;
	}

	return base;
}




void axc51base_cpu_device::check_irqs()
{
	// TODO: this logic is not correct

	if (!GET_EA)
		return;

	uint16_t base = get_irq_base();

	if (!base)
		return;

	if (m_timer0irq && GET_T0IRQEN)
	{
		push_pc();
		m_pc = base + V_TIMER0;
		m_timer0irq = false;
	}
	else if (m_dactimerirq && GET_DMAIRQEN)
	{
		push_pc();
		m_pc = base + V_DAC;
		m_dactimerirq = false;
	}
}




void axc51base_cpu_device::execute_set_input(int irqline, int state)
{
	uint32_t new_state = (m_last_line_state & ~(1 << irqline)) | ((state != CLEAR_LINE) << irqline);
	/* detect 0->1 transitions */
	//uint32_t tr_state = (~m_last_line_state) & new_state;

	// TODO

	m_last_line_state = new_state;
}

/* Execute cycles - returns number of cycles actually run */
void axc51base_cpu_device::execute_run()
{
	uint8_t op;

	/* external interrupts may have been set since we last checked */
	m_inst_cycles = 0;
	check_irqs();

	m_icount -= m_inst_cycles;

	do
	{
		/* Read next opcode */
		m_ppc = m_pc;
		
		op = prog_read(m_pc++);

		/* process opcode and count cycles */
		m_inst_cycles = axc51_cycles[op];
		execute_op(op);

		/* burn the cycles */
		m_icount -= m_inst_cycles;

		check_irqs();

	} while( m_icount > 0 );
}

uint8_t axc51base_cpu_device::xsfr_read(offs_t offset)
{
	offset &= 0x7f;

	LOGMASKED(LOG_UNHANDLED_XSFR,"%s: reading unhandled XSFR reg %04x\n", machine().describe_context(), offset + 0x3000);

	return m_xsfr_regs[offset];
}

void axc51base_cpu_device::xsfr_write(offs_t offset, uint8_t data)
{
	offset &= 0x7f;

	switch (offset)
	{
	case XSFR_PUP0: // 0x3010
	case XSFR_PUP1: // 0x3011
	case XSFR_PUP2: // 0x3012
	case XSFR_PUP3: // 0x3013
	case XSFR_PUP4: // 0x3014

	case XSFR_PDN0: // 0x3015
	case XSFR_PDN1: // 0x3016
	case XSFR_PDN2: // 0x3017
	case XSFR_PDN3: // 0x3018
	case XSFR_PDN4: // 0x3019
		break;

	case XSFR_PHD0: // 0x301a
	case XSFR_PHD1: // 0x301b
	case XSFR_PHD2: // 0x301c
	case XSFR_PHD3: // 0x301d
	case XSFR_PHD4: // 0x301e
		break;

	default:
		LOGMASKED(LOG_UNHANDLED_XSFR,"%s: writing to unhandled XSFR reg %04x data %02x\n", machine().describe_context(), offset + 0x3000, data);
		break;

	}
	m_xsfr_regs[offset] = data;
}


void axc51base_cpu_device::sfr_write(size_t offset, uint8_t data)
{
	/* update register */
	switch (offset)
	{
	case SFR_P0:   write_port(0, data);       break;
	case SFR_P1:   write_port(1, data);       break;
	case SFR_P2:   write_port(2, data);       break;
	case SFR_P3:   write_port(3, data);       break;
	case SFR_PSW:  SET_PARITY();              break;
	case SFR_ACC:  SET_PARITY();              break;
	case SFR_IP:   break;

	case SFR_B:
	case SFR_SP:
	case SFR_DPL0:
	case SFR_DPH0:
	case SFR_PCON:
		break;

	case SFR_DPL1: // 0x84
	case SFR_DPH1: // 0x85
		break;

	case SFR_IE:
		break;

	case SFR_IE1:
		break;

	case SFR_GP0: // 0xa1
	case SFR_GP1: // 0xa2
	case SFR_GP2: // 0xa3
	case SFR_GP3: // 0xa4
	case SFR_GP4: // 0xb1
	case SFR_GP5: // 0xb2
	case SFR_GP6: // 0xb3
	case SFR_GP7: // 0xb5
		break;

	case SFR_DACLCH: // 0xa6
		m_dac_out_cb[0](data);
		break;

	case SFR_DACRCH: // 0xa7
		m_dac_out_cb[1](data);
		break;

	case SFR_P0DIR: // 0xba
	case SFR_P1DIR: // 0xbb
	case SFR_P2DIR: // 0xbc
	case SFR_P3DIR: // 0xbd
	case SFR_P4DIR: // 0xbe
		break;

	case SFR_ER00: // 0xe6
	case SFR_ER01: // 0xe7
	case SFR_ER10: // 0xe8
	case SFR_ER11: // 0xe9
	case SFR_ER20: // 0xea
	case SFR_ER21: // 0xeb
	case SFR_ER30: // 0xec
	case SFR_ER31: // 0xed
	case SFR_ER8:  // 0xee
		break;

	case SFR_P4: write_port(4, data); break; // 0xb4

	case SFR_TMR0CON: // 0xf8
	case SFR_TMR0CNT: // 0xf9
	case SFR_TMR0PR:  // 0xfa
	case SFR_TMR0PSR: // 0xfb
		break;

	case SFR_IE2CRPT: // 0x95 controls automatic encryption
		ie2crypt_w(data);
		return;

	case SFR_DPCON: dpcon_w(data); return; // 0x86

	case SFR_DBASE: // 0x9b
		m_sfr_regs[SFR_DBASE] = data;
		return;


	case SFR_SPIDMAADR: spidmaadr_w(data); return; // 0xd6
	case SFR_SPIDMACNT: spidmacnt_w(data); return; // 0xd7
	case SFR_SPICON: spicon_w(data); return; // 0xd8
	case SFR_SPIBUF: spibuf_w(data); return; // 0xd9
	case SFR_SPIBAUD: spibaud_w(data); return; // 0xda


	default:
		LOGMASKED(LOG_UNHANDLED,"%s: attemping to write to an invalid/non-implemented SFR address: %02x  data=%02x\n", machine().describe_context(), (uint32_t)offset, data);
		/* no write in this case according to manual */
		return;
	}
	m_sfr_regs[offset] = data;
}

uint8_t axc51base_cpu_device::read_port(int i)
{
	uint8_t latched_out_data = 0x00;
	uint8_t port_direction = 0x00;
	uint8_t pup = 0x00;
	uint8_t pdn = 0x00;

	// direction 0xff = all bits set to input?

	// pdn and pup registers are mentioned as 'pull down' and 'pull up' but other than
	// there being 5 of them it isn't clear if they're used for these ports or not

	switch (i)
	{
	case 0: latched_out_data = P0; port_direction = m_sfr_regs[SFR_P0DIR]; pup = m_xsfr_regs[XSFR_PUP0]; pdn = m_xsfr_regs[XSFR_PDN0]; break;
	case 1: latched_out_data = P1; port_direction = m_sfr_regs[SFR_P1DIR]; pup = m_xsfr_regs[XSFR_PUP1]; pdn = m_xsfr_regs[XSFR_PDN1]; break;
	case 2: latched_out_data = P2; port_direction = m_sfr_regs[SFR_P2DIR]; pup = m_xsfr_regs[XSFR_PUP2]; pdn = m_xsfr_regs[XSFR_PDN2]; break;
	case 3: latched_out_data = P3; port_direction = m_sfr_regs[SFR_P3DIR]; pup = m_xsfr_regs[XSFR_PUP3]; pdn = m_xsfr_regs[XSFR_PDN3]; break;
	case 4: latched_out_data = P4; port_direction = m_sfr_regs[SFR_P4DIR]; pup = m_xsfr_regs[XSFR_PUP4]; pdn = m_xsfr_regs[XSFR_PDN4]; break;
	}

	uint8_t incoming = m_port_in_cb[i]();

	LOGMASKED(LOG_PORTS,"%s: reading port %d with direction %02x pup %02x pdn %02x latched output %02x incoming data %02x\n", machine().describe_context(), i, port_direction, pup, pdn, latched_out_data, incoming);
	return incoming;
}

void axc51base_cpu_device::write_port(int i, uint8_t data)
{
	uint8_t port_direction = 0x00;
	uint8_t pup = 0x00;
	uint8_t pdn = 0x00;

	switch (i)
	{
	case 0: port_direction = m_sfr_regs[SFR_P0DIR]; pup = m_xsfr_regs[XSFR_PUP0]; pdn = m_xsfr_regs[XSFR_PDN0]; break;
	case 1: port_direction = m_sfr_regs[SFR_P1DIR]; pup = m_xsfr_regs[XSFR_PUP1]; pdn = m_xsfr_regs[XSFR_PDN1]; break;
	case 2: port_direction = m_sfr_regs[SFR_P2DIR]; pup = m_xsfr_regs[XSFR_PUP2]; pdn = m_xsfr_regs[XSFR_PDN2]; break;
	case 3: port_direction = m_sfr_regs[SFR_P3DIR]; pup = m_xsfr_regs[XSFR_PUP3]; pdn = m_xsfr_regs[XSFR_PDN3]; break;
	case 4: port_direction = m_sfr_regs[SFR_P4DIR]; pup = m_xsfr_regs[XSFR_PUP4]; pdn = m_xsfr_regs[XSFR_PDN4]; break;
	}

	LOGMASKED(LOG_PORTS,"%s: writing port %d with direction %02x pup %02x pdn %02x data %02x\n", machine().describe_context(), i, port_direction, pup, pdn, data);
	m_port_out_cb[i](data); // also send port direction??
}

uint8_t axc51base_cpu_device::sfr_read(size_t offset)
{
	switch (offset)
	{
	case SFR_P0:   return read_port(0);
	case SFR_P1:   return read_port(1);
	case SFR_P2:   return read_port(2);
	case SFR_P3:   return read_port(3);

	case SFR_PSW:
	case SFR_ACC:
	case SFR_B:
	case SFR_SP:
	case SFR_DPL0:
	case SFR_DPH0:
	case SFR_PCON:
	case SFR_IE:
	case SFR_IE1:

	case SFR_DPL1: // 0x84
	case SFR_DPH1: // 0x85

	case SFR_IP:

	case SFR_GP0: // 0xa1
	case SFR_GP1: // 0xa2
	case SFR_GP2: // 0xa3
	case SFR_GP3: // 0xa4
	case SFR_GP4: // 0xb1
	case SFR_GP5: // 0xb2
	case SFR_GP6: // 0xb3
	case SFR_GP7: // 0xb5
		return m_sfr_regs[offset];

	case SFR_P4: // 0xb4
		return read_port(4);

	case SFR_P0DIR: // 0xba
	case SFR_P1DIR: // 0xbb
	case SFR_P2DIR: // 0xbc
	case SFR_P3DIR: // 0xbd
	case SFR_P4DIR: // 0xbe

	case SFR_ER00: // 0xe6
	case SFR_ER01: // 0xe7
	case SFR_ER10: // 0xe8
	case SFR_ER11: // 0xe9
	case SFR_ER20: // 0xea
	case SFR_ER21: // 0xeb
	case SFR_ER30: // 0xec
	case SFR_ER31: // 0xed
	case SFR_ER8:  // 0xee

	case SFR_TMR0CON: // 0xf8
	case SFR_TMR0CNT: // 0xf9
	case SFR_TMR0PR:  // 0xfa
	case SFR_TMR0PSR: // 0xfb

	case SFR_IE2CRPT: // 0x95 controls automatic encryption
		return m_sfr_regs[offset];


	case SFR_DPCON: // 0x86
		return dpcon_r();

	case SFR_IRTCON: // 0x9f
		return 0x00;// (uint8_t)rand();

	case SFR_SPICON: // 0xd8
		return spicon_r();

	case SFR_SPIBUF: // 0xd9
		return spibuf_r();

	case SFR_UID0: return m_uid[0]; // 0xe2 Chip-ID, can only be read from code in internal area?
	case SFR_UID1: return m_uid[1]; // 0xe3
	case SFR_UID2: return m_uid[2]; // 0xe4
	case SFR_UID3: return m_uid[3]; // 0xe5

	case SFR_LFSRFIFO: // 0xf6
		return 0x00;// (uint8_t)rand();

	case SFR_UARTSTA: // 0xfc
		return uartsta_r();

		/* Illegal or non-implemented sfr */
	default:
		LOGMASKED(LOG_UNHANDLED,"%s: attemping to read an invalid/non-implemented SFR address: %02x\n", machine().describe_context(), (uint32_t)offset);
		/* according to the manual, the read may return random bits */
		return 0xff;
	}
}


void axc51base_cpu_device::device_start()
{
}

/* Reset registers to the initial values */
void axc51base_cpu_device::device_reset()
{
	m_last_line_state = 0;

	/* Flag as NO IRQ in Progress */
	m_last_op = 0;
	m_last_bit = 0;

	/* these are all defined reset states */
	m_ppc = m_pc;
	m_pc = 0;
	SP = 0x7;
	SET_PSW(0);
	SET_ACC(0);
	DPH0 = 0;
	DPL0 = 0;
	B = 0;
	IP = 0;
	IE = 0;
	PCON = 0;

	/* set the port configurations to all 1's */
	SET_P3(0xff);
	SET_P2(0xff);
	SET_P1(0xff);
	SET_P0(0xff);

	m_recalc_parity = 0;

	m_spi_dma_addr = 0;

//  m_timer0->adjust(attotime::never);
//  m_dactimer->adjust(attotime::never);


	m_timer0irq = false;
	m_dactimerirq = false;
}






/*

SFR_SPICON (at 0xd8)

7  SPIPND  (0 = Send not finished, 1 = finished)
6  SPISM   (0 = Master, 1 = Slave)
5  SPIRT   (RX/TX select for 2-wire mode / DMA, 0 = TX, 1 = RX)
4  SPIWS   (0 = 3-wire mode, 1 = 2-wire mode)
3  SPIGSEL (0 = group 0, 1 = group 1)
2  SPIEDGE (if SPIIDST == 0 then 0 = falling edge, 1 = rising edge, if SPIIDST == 1 inverted)
1  SPIDST  (0 = clock signal is 0 when idle, 1 = clock signal is 1 when idle)
0  SPIEN   (0 = SPI disable, 1 = enable)
*/

uint8_t axc51base_cpu_device::spicon_r()
{
	uint8_t result = m_sfr_regs[SFR_SPICON] | 0x80;
//  LOGMASKED(LOG_UNSORTED,"%s: sfr_read SFR_SPICON %02x\n", machine().describe_context(), result);
	return result;
}

/*

SFR_UARTSTA (at 0xfc)

7 UTRXNB      (9th bit of data of RX buffer)
6 FEF         (0 = stop bit was 1 in last frame,  1 = stop bit was 0)
5 RXIF        (0 = receive not done, 1 = done)
4 TXIF        (0 = transmit not done, 1 = done)
3 ---
2 ---
1 ---
0 PSEL        (UART port / pin select)

*/

uint8_t axc51base_cpu_device::uartsta_r()
{
	//uint8_t result = m_sfr_regs[SFR_UARTSTA];
	uint8_t result = 0x30;
	LOGMASKED(LOG_UNSORTED, "%s: sfr_read SFR_UARTSTA %02x\n", machine().describe_context(), result);
	return result;
}


void axc51base_cpu_device::spicon_w(uint8_t data)
{
//  LOGMASKED(LOG_UNSORTED,"%s: sfr_write SFR_SPICON %02x\n", machine().describe_context(), data);
	m_sfr_regs[SFR_SPICON] = data;
}



uint8_t axc51base_cpu_device::dpcon_r()
{
	LOGMASKED(LOG_UNSORTED,"%s: sfr_read SFR_DPCON\n", machine().describe_context());
	return m_sfr_regs[SFR_DPCON];
}

uint8_t axc51base_cpu_device::spibuf_r()
{
	// TODO: encryption here (if enabled)
	uint8_t ret = m_spi_in_cb();
	if (m_sfr_regs[SFR_IE2CRPT] & 0x03)
		ret = (uint8_t)rand();

	return ret;
}

void axc51base_cpu_device::spibuf_w(uint8_t data)
{
	// TODO: encryption here (if enabled)
	m_spi_out_cb(data);
}

void axc51base_cpu_device::spibaud_w(uint8_t data)
{
	LOGMASKED(LOG_UNSORTED,"%s: sfr_write SFR_SPIBAUD %02x\n", machine().describe_context(), data);
	m_sfr_regs[SFR_SPIBAUD] = data;
}

/*
SFR_DPCON (at 0x86)

7  IA   01 = vector base 0x4003, 10 = vector base 0x8003, 00/11 invalid
6  IA
5  DPID0  DPTR0 increase direction control, 0 = increase, 1 = decrease
4  DPID1  DPTR1 increase direction control, 0 = increase, 1 = descrese
3  DPAID  DPTR auto increase enable
2  DPTSL  DPSEL auto-toggle enable (0 = no auto toggle, 1 = auto toggle)
1  ---
0  DPSEL  DPTR Select (0 = use DPTR0, 1 = use DPTR1)
*/

void axc51base_cpu_device::dpcon_w(uint8_t data)
{
	m_sfr_regs[SFR_DPCON] = data;
}

/*
SFR_IE2CRPT (at 0x95)

7  ----
6  ----
5  wdt_int_enable
4  soft_int
3  sd_do_crypt
2  sd_di_crypt
1  spi_do_crypt
0  spi_di_crypt

*/

void axc51base_cpu_device::ie2crypt_w(uint8_t data)
{
	LOGMASKED(LOG_UNSORTED,"%s: sfr_write SFR_IE2CRPT %02x\n", machine().describe_context(), data);
	m_sfr_regs[SFR_IE2CRPT] = data;

	if (data & 0x03)
	{
		LOGMASKED(LOG_UNSORTED,"SPI encryption turned on!\n");
	}

	if (data & 0x0c)
	{
		LOGMASKED(LOG_UNSORTED,"SD Card encryption turned on!\n");
	}
}




void axc51base_cpu_device::spidmaadr_w(uint8_t data)
{
	m_sfr_regs[SFR_SPIDMAADR] = data;

	m_spi_dma_addr <<= 8;
	m_spi_dma_addr = (m_spi_dma_addr & 0xff00) | data;

}

void axc51base_cpu_device::spidmacnt_w(uint8_t data)
{
	m_sfr_regs[SFR_SPIDMACNT] = data;

	if (((m_sfr_regs[SFR_SPICON]) & 0x20) == 0x20) // Read from SPI
	{
		for (int i = 0; i < (data + 1) * 2; i++)
		{
			spibuf_w(0x00); // clock
			uint8_t romdat = spibuf_r();
			io_write(m_spi_dma_addr++, romdat); // is this the correct destination space?
		}
	}
	else
	{
		for (int i = 0; i < (data + 1) * 2; i++)
		{
			uint8_t ramdat = io_read(m_spi_dma_addr++);
			spibuf_w(ramdat);
		}

	}
}

void ax208_cpu_device::reset()
{
	axc51base_cpu_device::device_reset();
	m_pc = 0x8000;
}
