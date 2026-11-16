// license:BSD-3-Clause
// copyright-holders:David Haywood, PocketChroma contributors
// Standalone handheld system, adapted from MAME's monon_color driver.
// Video/input/music-link logic follows the MAME driver; scheduling, audio
// resampling, saves and save states are PocketChroma additions.
#include "system.h"

namespace chroma {

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return 0xff000000u | (r << 16) | (g << 8) | b; }
static inline uint32_t scale8(uint32_t c, uint8_t s)
{
	uint32_t r = ((c >> 16) & 0xff) * s / 255, g = ((c >> 8) & 0xff) * s / 255, b = (c & 0xff) * s / 255;
	return 0xff000000u | (r << 16) | (g << 8) | b;
}
static inline uint32_t addsat(uint32_t a, uint32_t b)
{
	uint32_t r = std::min<uint32_t>(255, ((a >> 16) & 0xff) + ((b >> 16) & 0xff));
	uint32_t g = std::min<uint32_t>(255, ((a >> 8) & 0xff) + ((b >> 8) & 0xff));
	uint32_t bl = std::min<uint32_t>(255, (a & 0xff) + (b & 0xff));
	return 0xff000000u | (r << 16) | (g << 8) | bl;
}

System::System() : m_vidbuffer(WIDTH * HEIGHT, 0xff000000u)
{
	memset(m_musicrom, 0xff, sizeof(m_musicrom));
	m_cpu.port_in_cb<0>().set([this] { return in0_r(); });
	m_cpu.port_in_cb<1>().set([this] { return in1_r(); });
	m_cpu.port_in_cb<2>().set([this] { return in2_r(); });
	m_cpu.port_in_cb<3>().set([this] { return in3_r(); });
	m_cpu.port_in_cb<4>().set([this] { return in4_r(); });
	m_cpu.port_out_cb<0>().set([this](uint8_t d) { out0_w(d); });
	m_cpu.port_out_cb<1>().set([this](uint8_t d) { out1_w(d); });
	m_cpu.port_out_cb<2>().set([this](uint8_t d) { out2_w(d); });
	m_cpu.port_out_cb<3>().set([this](uint8_t d) { out3_w(d); });
	m_cpu.port_out_cb<4>().set([this](uint8_t d) { out4_w(d); });
	m_cpu.spi_in_cb().set([this] { return spibuf_r(); });
	m_cpu.spi_out_cb().set([this](uint8_t d) { spibuf_w(d); });
	m_cpu.dac_out_cb<0>().set([this](uint8_t d) { dacout0_w(d); });
	m_cpu.start();
}

bool System::load_bios(const uint8_t *data, size_t size)
{
	if (size != 0x2000) return false;
	memcpy(m_cpu.m_rom, data, 0x2000);
	m_bios_ok = true;
	return true;
}

bool System::load_cart(const uint8_t *data, size_t size)
{
	if (size < 0x10000 || (size & (size - 1)) != 0) return false;
	m_flash.assign(data, data + size);
	m_spi.set_rom(m_flash.data(), m_flash.size());
	// The cart's music MCU protection check compares MCU replies against
	// a copy of the MCU program stored in the flash at 0xe000-0xefff.
	memcpy(m_musicrom, m_flash.data() + 0xe000, 0x1000);
	return true;
}

bool System::load_save(const uint8_t *data, size_t size)
{
	if (m_flash.size() < SAVE_BASE + SAVE_SIZE || size != SAVE_SIZE) return false;
	memcpy(m_flash.data() + SAVE_BASE, data, SAVE_SIZE);
	return true;
}

void System::reset()
{
	m_dacbyte = 0;
	m_out0data = m_out1data = m_out2data = m_out3data = m_out4data = 0;
	m_bufpos_x = 0;
	m_bufpos_y = 239;
	std::fill(std::begin(m_linebuf), std::end(m_linebuf), 0xff000000u);
	std::fill(m_vidbuffer.begin(), m_vidbuffer.end(), 0xff000000u);
	memset(m_storeregs, 0, sizeof(m_storeregs));
	memset(m_curpal, 0, sizeof(m_curpal));
	m_music_direction_iswrite = true;
	m_music_latch = 0;
	m_music_bitpos = 0;
	m_music_response_bit = 0;
	m_music_bits_in_needed = 0;
	m_cycle_accum = m_timer0_ctr = m_dac_ctr = m_audio_ctr = 0;
	m_dac_level = 0;
	m_spi.reset();
	m_cpu.reset();
}

void System::run_frame(int fps)
{
	m_audio.clear();
	const int64_t frame_cycles = CPU_CLOCK / fps;
	const int64_t timer0_period = CPU_CLOCK / 120;
	const int64_t dac_period = CPU_CLOCK / 8000;
	const int64_t audio_period = CPU_CLOCK / AUDIO_RATE; // 1500
	const int slice = (int)audio_period;

	m_cycle_accum += frame_cycles;
	while (m_cycle_accum > 0)
	{
		int ran = m_cpu.run(slice);
		m_cycle_accum -= ran;

		m_timer0_ctr += ran;
		while (m_timer0_ctr >= timer0_period) { m_timer0_ctr -= timer0_period; m_cpu.fire_timer0(); }
		m_dac_ctr += ran;
		while (m_dac_ctr >= dac_period) { m_dac_ctr -= dac_period; m_cpu.fire_dactimer(); }
		m_audio_ctr += ran;
		while (m_audio_ctr >= audio_period)
		{
			m_audio_ctr -= audio_period;
			// simple DC-blocking high-pass so silence settles at zero
			int32_t in = m_dac_level;
			int32_t out = in - m_hp_prev_in + (m_hp_prev_out * 255) / 256;
			m_hp_prev_in = in; m_hp_prev_out = out;
			m_audio.push_back((int16_t)std::clamp(out, -32768, 32767));
		}
	}
}

// ---------------------------------------------------------------------------
// inputs

uint8_t System::controls(int n) const
{
	const uint32_t b = m_buttons;
	switch (n)
	{
	case 0: return ((b & BTN_B) ? 0x02 : 0) | ((b & BTN_A) ? 0x04 : 0);
	case 1: return ((b & BTN_DOWN) ? 0x01 : 0) | ((b & BTN_RIGHT) ? 0x02 : 0) | ((b & BTN_R) ? 0x04 : 0);
	case 2: return ((b & BTN_UP) ? 0x01 : 0) | ((b & BTN_LEFT) ? 0x02 : 0) | ((b & BTN_L) ? 0x04 : 0);
	case 3: return ((b & BTN_MENU) ? 0x01 : 0) | ((b & BTN_PAUSE) ? 0x02 : 0);
	}
	return 0;
}

uint8_t System::read_current_inputs()
{
	uint8_t iomux = (m_out1data & 0x18) >> 3;
	if ((m_out4data & 0x04) == 0x00)
	{
		switch (iomux)
		{
		case 0: return (controls(0) | controls(1) | controls(2) | controls(3)) ^ 0x7;
		case 1: return controls(0) ^ 0x7;
		case 2: return controls(1) ^ 0x7;
		case 3: return controls(2) ^ 0x7;
		}
	}
	else
		return controls(3) ^ 0x7;
	return 0x00;
}

uint8_t System::in0_r()
{
	uint8_t in = read_current_inputs();
	uint8_t ret = 0xff & 0xcf;
	if (in & 0x01) ret |= 0x10;
	if (in & 0x02) ret |= 0x20;
	return ret;
}

uint8_t System::in1_r() { return (0xff & 0xe7) | (m_out1data & 0x18); }

uint8_t System::in2_r()
{
	uint8_t in = read_current_inputs();
	uint8_t ret = m_out2data & 0x7b;
	ret |= m_music_response_bit << 7;
	if (in & 0x04) ret |= 0x04;
	return ret;
}

uint8_t System::in3_r() { return m_out3data; }
uint8_t System::in4_r() { return (0xff & 0xfb) | (m_out4data & 0x04); }

// ---------------------------------------------------------------------------
// outputs

void System::dacout0_w(uint8_t data)
{
	m_dacbyte ^= 1;
	if (m_dacbyte == 1)
		m_dac_level = (int16_t)((int8_t)data * 128); // 8-bit twos complement R2R DAC
}

void System::out0_w(uint8_t data) { m_out0data = data & 0x1f; }

void System::out1_w(uint8_t data)
{
	// e5 -> e7 latches a write of P3 to the video device, e3 -> e7 a read
	if ((m_out1data == 0xe5) && (data == 0xe7))
		write_to_video_device(m_out3data);
	else if ((m_out1data == 0xe3) && (data == 0xe7))
		m_out3data = read_from_video_device();
	m_out1data = data;
}

void System::out3_w(uint8_t data) { m_out3data = data; }
void System::out4_w(uint8_t data) { m_out4data = data; }

void System::get_music_command_bit(uint8_t bit)
{
	bit &= 1;
	if (m_music_bitpos == 0)
	{
		m_music_bitpos++;
		return;
	}

	if (m_music_direction_iswrite)
	{
		m_music_latch = (m_music_latch << 1) | bit;
		m_music_bitpos++;
		if (m_music_bitpos == 17)
		{
			m_music_bitpos = 0;
			if ((m_music_latch & 0xf000) == 0xa000)
			{
				// protection: MCU replies with a byte of its program, which
				// the game checks against the copy in flash
				m_music_direction_iswrite = false;
				m_music_bits_in_needed = 9;
				m_music_latch = m_musicrom[m_music_latch & 0xfff];
			}
			else
			{
				// 0xxx volume, 2xxx play music, 3xxx stop, 4xxx reset:
				// the cartridge's music chip is undumped so music is silent
				m_music_latch = 0;
			}
		}
	}
	else
	{
		m_music_response_bit = (m_music_latch & 0x80) >> 7;
		m_music_latch <<= 1;
		m_music_bitpos++;
		if (m_music_bitpos == m_music_bits_in_needed)
		{
			m_music_bitpos = 0;
			m_music_direction_iswrite = true;
		}
	}
}

void System::out2_w(uint8_t data)
{
	// m-C- --Ms : m = MCU data in, C = MCU clock, M = MCU data out, s = SPI CE
	if ((data & 0x01) != (m_out2data & 0x01) && !(data & 0x01))
		m_spi.set_ready();

	if ((data & 0x20) != (m_out2data & 0x20) && !(data & 0x20))
		get_music_command_bit((data & 0x02) >> 1);

	m_out2data = data;
}

// ---------------------------------------------------------------------------
// video

uint8_t System::read_from_video_device()
{
	if (m_out0data == 0x0c) return 0xff ^ 0x02;
	return 0x00;
}

void System::do_draw_inner(int pal_to_use, int start, int step, int pixmask, int amount)
{
	uint8_t blend = (m_storeregs[0x1c] == 0x0a) ? ((m_storeregs[0x09] & 0x3f) << 2) : 0;

	int yadjust = (m_storeregs[0x16] << 8) | m_storeregs[0x1a];
	yadjust >>= 7;
	yadjust -= 8;

	for (int n = 0; n < amount; n++)
	{
		spibuf_w(0x00); // clock
		uint8_t pix = spibuf_r();

		for (int i = start; i >= 0; i -= step)
		{
			int real_ypos = m_bufpos_y - yadjust;
			if ((real_ypos >= 0) && (real_ypos < HEIGHT))
			{
				uint8_t pixx = (pix >> i) & pixmask;
				int idx = ((pixx + pal_to_use) & 0x7ff) * 3;
				uint8_t r = m_curpal[idx + 2], g = m_curpal[idx + 1], b = m_curpal[idx + 0];

				if (!(r == 0xdc && g == 0x32 && b == 0xdc)) // magic transparency colour
				{
					uint32_t c = rgb(r, g, b);
					if (blend != 0x00)
						m_linebuf[real_ypos] = addsat(scale8(m_linebuf[real_ypos], 0xff - blend), scale8(c, blend));
					else
						m_linebuf[real_ypos] = c;
				}
			}
			m_bufpos_y--;
		}
	}
}

void System::do_draw(int amount, int pal_to_use)
{
	switch (m_storeregs[0x12])
	{
	case 0x13: do_draw_inner(pal_to_use, 0, 1, 0xff, amount); break; // 8bpp
	case 0x12: do_draw_inner(pal_to_use, 4, 4, 0x0f, amount); break; // 4bpp
	case 0x11: do_draw_inner(pal_to_use, 6, 2, 0x03, amount); break; // 2bpp
	case 0x10: do_draw_inner(pal_to_use, 7, 1, 0x01, amount); break; // 1bpp
	}
	m_bufpos_y = 239;
}

void System::do_palette(int amount, int pal_to_use)
{
	for (int i = 0; i < amount; i++)
	{
		spibuf_w(0x00); // clock
		int address = i + pal_to_use * 3;
		if (address < (int)sizeof(m_curpal))
			m_curpal[address] = spibuf_r();
	}
}

void System::write_to_video_device(uint8_t data)
{
	if (m_out4data == 0x03)
	{
		if (m_out0data >= 0x10 && m_out0data < 0x20)
			m_storeregs[m_out0data] = data;
	}
	else if (m_out4data == 0x09)
	{
		if (m_out0data < 0x10)
			m_storeregs[m_out0data] = data;
	}

	// direct palette uploads (used for 1bpp text)
	if (m_out0data == 0x00 && m_storeregs[0x04] == 0x8e)
	{
		int address = (m_storeregs[0x08]++) + 0x400 * 3;
		if (address < (int)sizeof(m_curpal))
			m_curpal[address] = data;
	}

	if (m_out0data == 0x1b && data == 0x81)
	{
		m_bufpos_x++;
		if (m_bufpos_x == WIDTH)
			m_bufpos_x = 0;
		m_bufpos_y = 239;
	}

	if (m_out0data == 0x0c)
	{
		uint8_t mode = m_storeregs[0x1c];
		if (mode == 0x00 || mode == 0x0a || mode == 0x01 || mode == 0x11)
		{
			uint16_t amount = m_storeregs[0x0e] | (m_storeregs[0x01] << 8);
			int pal_to_use = ((m_storeregs[0x11] << 8) | m_storeregs[0x1e]) >> 5;
			pal_to_use &= 0x7ff;

			if (data == 0xde || data == 0xda)
			{
				if (m_storeregs[0x02] == 0x20)
					do_palette(amount, pal_to_use);
			}
			else if (data == 0xd2 || data == 0xd6)
			{
				if (m_storeregs[0x02] == 0x20)
					do_draw(amount, pal_to_use);
			}
			else if (data == 0xd0 || data == 0xd4)
			{
				// column buffer swap
				for (int i = 0; i < HEIGHT; i++)
					m_vidbuffer[(i * WIDTH) + m_bufpos_x] = m_linebuf[i];
			}
		}
	}
}

// ---------------------------------------------------------------------------
// save states

template <typename F> void System::serialize(F &&f)
{
	m_cpu.serialize(f);
	m_spi.serialize(f);
	f(&m_cycle_accum, sizeof(m_cycle_accum)); f(&m_timer0_ctr, sizeof(m_timer0_ctr));
	f(&m_dac_ctr, sizeof(m_dac_ctr)); f(&m_audio_ctr, sizeof(m_audio_ctr)); f(&m_dac_level, sizeof(m_dac_level));
	f(m_linebuf, sizeof(m_linebuf)); f(m_vidbuffer.data(), m_vidbuffer.size() * 4);
	f(&m_bufpos_y, sizeof(m_bufpos_y)); f(&m_bufpos_x, sizeof(m_bufpos_x));
	f(m_storeregs, sizeof(m_storeregs)); f(&m_dacbyte, 1);
	f(&m_out0data, 1); f(&m_out1data, 1); f(&m_out2data, 1); f(&m_out3data, 1); f(&m_out4data, 1);
	f(m_curpal, sizeof(m_curpal));
	f(&m_music_direction_iswrite, sizeof(m_music_direction_iswrite)); f(&m_music_latch, sizeof(m_music_latch));
	f(&m_music_bitpos, 1); f(&m_music_response_bit, 1); f(&m_music_bits_in_needed, 1);
	// save area of the flash
	if (m_flash.size() >= SAVE_BASE + SAVE_SIZE)
		f(m_flash.data() + SAVE_BASE, SAVE_SIZE);
}

static const uint32_t STATE_MAGIC = 0x43524d31; // "CRM1"

std::vector<uint8_t> System::save_state()
{
	std::vector<uint8_t> out;
	out.insert(out.end(), (const uint8_t *)&STATE_MAGIC, (const uint8_t *)&STATE_MAGIC + 4);
	serialize([&](void *p, size_t n) { out.insert(out.end(), (uint8_t *)p, (uint8_t *)p + n); });
	return out;
}

bool System::load_state(const std::vector<uint8_t> &s)
{
	size_t need = 4;
	serialize([&](void *, size_t n) { need += n; });
	if (s.size() != need || memcmp(s.data(), &STATE_MAGIC, 4) != 0) return false;
	size_t pos = 4;
	serialize([&](void *p, size_t n) { memcpy(p, s.data() + pos, n); pos += n; });
	return true;
}

} // namespace chroma
