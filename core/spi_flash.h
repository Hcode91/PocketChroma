// license:BSD-3-Clause
// copyright-holders:David Haywood
// Adapted for PocketChroma from MAME's generic_spi_flash device (byte-level HLE).
#pragma once
#include "emu.h"

class spi_flash
{
public:
	void set_rom(uint8_t *rom, size_t size) { m_spiptr = rom; m_length = size; }
	uint8_t read() { return m_spi_latch; }
	void set_ready() { m_spi_state = READY_FOR_COMMAND; m_spi_state_step = 0; }
	void write(uint8_t data);
	void reset()
	{
		m_spi_addr = 0; m_spi_state = 0; m_spi_latch = 0; m_spi_state_step = 0;
		m_spi_statusreg = 0; m_spi_configreg = 0;
	}
	// set whenever a byte is programmed so the frontend knows to flush saves
	bool m_dirty = false;

	template <typename F> void serialize(F &&f)
	{
		f(&m_spi_addr, sizeof(m_spi_addr)); f(&m_spi_state, 1); f(&m_spi_latch, 1);
		f(&m_spi_state_step, 1); f(&m_spi_statusreg, 1); f(&m_spi_configreg, 1);
	}

private:
	enum
	{
		READY_FOR_COMMAND = 0x00,
		COMMAND_01_WRSR = 0x01, COMMAND_02_PP = 0x02, COMMAND_03_READ = 0x03, COMMAND_04_WRDI = 0x04,
		COMMAND_05_RDSR = 0x05, COMMAND_06_WREN = 0x06, COMMAND_0B_FAST_READ = 0x0b, COMMAND_11_UNKNOWN = 0x11,
		COMMAND_15_RDCR = 0x15, COMMAND_20_SE = 0x20, COMMAND_31_UNKNOWN = 0x31, COMMAND_35_RDSR2 = 0x35,
		COMMAND_50_VSR_WREN = 0x50, COMMAND_66_ENABLE_RESET = 0x66, COMMAND_90_REMS = 0x90, COMMAND_99_RESET = 0x99,
		COMMAND_9F_RDID = 0x9f, COMMAND_AB_RDP = 0xab, COMMAND_B9_DP = 0xb9, COMMAND_EB_4READ = 0xeb,
		COMMAND_EC_UNKNOWN = 0xec, COMMAND_FF_CRMR = 0xff,
	};
	void get_command(uint8_t data);
	void set_addr_byte(uint8_t data);

	uint32_t m_spi_addr = 0;
	uint8_t m_spi_state = 0, m_spi_latch = 0, m_spi_state_step = 0, m_spi_statusreg = 0, m_spi_configreg = 0;
	uint8_t *m_spiptr = nullptr;
	size_t m_length = 0;
	const uint8_t m_idbytes[3] = { 0xfe, 0xfe, 0x00 };
};
