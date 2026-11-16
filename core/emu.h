// license:BSD-3-Clause
// PocketChroma - minimal MAME-compatibility shim so the MAME AXC51 core
// (BSD-3-Clause, see LICENSE-MAME) can be built standalone.
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <algorithm>
#include <utility>
#include <vector>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint32_t offs_t;

#define ATTR_COLD
#define NAME(x) x
#define CLEAR_LINE 0
#define ASSERT_LINE 1
#define BIT(x, n) (((x) >> (n)) & 1)

[[noreturn]] inline void fatalerror_impl(const char *msg)
{
	fprintf(stderr, "PocketChroma fatal: %s\n", msg);
	abort();
}
// Non-fatal in release: just log. Games occasionally hit unimplemented paths.
#define fatalerror(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)

// LOGMASKED / LOG are compiled out
#define LOGMASKED(...) do { } while (0)
#define LOG(...) do { } while (0)

// devcb replacements -------------------------------------------------------
struct devcb_read8
{
	std::function<uint8_t()> f;
	uint8_t def = 0xff;
	devcb_read8() {}
	struct binder { devcb_read8 &d; void set(std::function<uint8_t()> fn) { d.f = std::move(fn); } };
	binder bind() { return binder{*this}; }
	uint8_t operator()() { return f ? f() : def; }
	template <unsigned N> using array = std::array<devcb_read8, N>;
};

struct devcb_write8
{
	std::function<void(uint8_t)> f;
	devcb_write8() {}
	struct binder { devcb_write8 &d; void set(std::function<void(uint8_t)> fn) { d.f = std::move(fn); } };
	binder bind() { return binder{*this}; }
	void operator()(uint8_t v) { if (f) f(v); }
	void operator()(offs_t, uint8_t v) { if (f) f(v); }
	template <unsigned N> using array = std::array<devcb_write8, N>;
};

// simple pointer wrapper standing in for required_shared_ptr
template <typename T>
struct shared_ptr_shim
{
	T *p = nullptr;
	T &operator[](size_t i) const { return p[i]; }
	T *target() const { return p; }
	operator T *() const { return p; }
};

struct machine_shim { const char *describe_context() const { return "axc51"; } uint8_t rand() const { return (uint8_t)::rand(); } };
inline machine_shim machine() { return machine_shim(); }
inline uint16_t swapendian_int16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
