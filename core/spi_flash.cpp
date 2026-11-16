// license:BSD-3-Clause
// copyright-holders:David Haywood
// Adapted for PocketChroma from MAME's generic_spi_flash device (byte-level HLE).
#include "spi_flash.h"

void spi_flash::get_command(uint8_t data)
{
	switch (data)
	{
	case COMMAND_01_WRSR: case COMMAND_9F_RDID: case COMMAND_03_READ: case COMMAND_05_RDSR:
	case COMMAND_0B_FAST_READ: case COMMAND_02_PP: case COMMAND_11_UNKNOWN: case COMMAND_15_RDCR:
	case COMMAND_20_SE: case COMMAND_31_UNKNOWN: case COMMAND_35_RDSR2: case COMMAND_90_REMS:
	case COMMAND_EB_4READ: case COMMAND_EC_UNKNOWN:
		m_spi_state = data;
		break;
	case COMMAND_06_WREN: m_spi_state = READY_FOR_COMMAND; m_spi_statusreg |= 0x02; break;
	case COMMAND_04_WRDI: m_spi_state = READY_FOR_COMMAND; m_spi_statusreg &= ~0x02; break;
	case COMMAND_50_VSR_WREN: case COMMAND_66_ENABLE_RESET: case COMMAND_99_RESET:
	case COMMAND_AB_RDP: case COMMAND_B9_DP: case COMMAND_FF_CRMR:
		m_spi_state = READY_FOR_COMMAND;
		break;
	default:
		fprintf(stderr, "SPI: unknown command %02x\n", data);
		m_spi_state = READY_FOR_COMMAND;
		break;
	}
	m_spi_state_step = 0;
}

void spi_flash::set_addr_byte(uint8_t data)
{
	switch (m_spi_state_step)
	{
	case 0: m_spi_addr = (m_spi_addr & 0x00ffff) | (data << 16); break;
	case 1: m_spi_addr = (m_spi_addr & 0xff00ff) | (data << 8); break;
	case 2: m_spi_addr = (m_spi_addr & 0xffff00) | data; break;
	}
	m_spi_state_step++;
}

void spi_flash::write(uint8_t data)
{
	const uint32_t mask = m_length ? (uint32_t)(m_length - 1) : 0;
	switch (m_spi_state)
	{
	case READY_FOR_COMMAND:
		get_command(data);
		break;

	case COMMAND_01_WRSR:
		if (m_spi_state_step == 0) { m_spi_configreg = data; m_spi_state_step++; }
		else { m_spi_statusreg = data; m_spi_state = READY_FOR_COMMAND; }
		break;

	case COMMAND_02_PP:
		if (m_spi_state_step < 3) set_addr_byte(data);
		else if ((m_spi_statusreg & 0x02) && m_spiptr)
		{
			m_spiptr[(m_spi_addr++) & mask] = data;
			m_dirty = true;
		}
		break;

	case COMMAND_03_READ:
		if (m_spi_state_step < 3) set_addr_byte(data);
		else if (m_spiptr) m_spi_latch = m_spiptr[(m_spi_addr++) & mask];
		break;

	case COMMAND_0B_FAST_READ:
		if (m_spi_state_step < 3) set_addr_byte(data);
		else if (m_spi_state_step == 3) m_spi_state_step++; // dummy
		else if (m_spiptr) m_spi_latch = m_spiptr[(m_spi_addr++) & mask];
		break;

	case COMMAND_EB_4READ:
		if (m_spi_state_step < 3) set_addr_byte(data);
		else if (m_spi_state_step < 6) m_spi_state_step++; // dummy
		else if (m_spiptr) m_spi_latch = m_spiptr[(m_spi_addr++) & mask];
		break;

	case COMMAND_05_RDSR:
		if (m_spi_state_step == 0) { m_spi_latch = m_spi_statusreg; m_spi_state_step++; }
		else { m_spi_latch = 0x00; m_spi_state = READY_FOR_COMMAND; }
		break;

	case COMMAND_15_RDCR:
		m_spi_latch = m_spi_configreg;
		m_spi_state = READY_FOR_COMMAND;
		break;

	case COMMAND_20_SE:
		// MAME does not erase here; real flash would set the 4KB sector to 0xff.
		// Games rewrite the save area afterwards, so erase it for correctness.
		if (m_spi_state_step < 3)
		{
			set_addr_byte(data);
			if (m_spi_state_step == 3 && m_spiptr && (m_spi_statusreg & 0x02))
			{
				uint32_t base = m_spi_addr & ~0xfffu & mask;
				// only allow erasing inside the save region so a bug can't wipe game data
				if (base >= 0x50000 && base < 0x60000)
				{
					memset(m_spiptr + base, 0xff, 0x1000);
					m_dirty = true;
				}
			}
		}
		break;

	case COMMAND_35_RDSR2:
		m_spi_latch = m_spi_statusreg;
		m_spi_state = READY_FOR_COMMAND;
		break;

	case COMMAND_90_REMS:
		if (m_spi_state_step < 4) m_spi_state_step++;
		else m_spi_state = READY_FOR_COMMAND;
		break;

	case COMMAND_9F_RDID:
		m_spi_latch = m_idbytes[m_spi_state_step < 3 ? m_spi_state_step : 2];
		if (m_spi_state_step < 2) m_spi_state_step++;
		break;

	default:
		break;
	}
}
