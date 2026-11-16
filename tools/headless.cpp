// Headless test runner: headless <ax208.bin> <cart.bin> <frames> [out.ppm]
#include "../core/system.h"
#include <fstream>
#include <iterator>
static std::vector<uint8_t> rd(const char *p){ std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: headless bios cart frames [out.ppm]\n"); return 1; }
	auto bios = rd(argv[1]); auto cart = rd(argv[2]); int frames = atoi(argv[3]);
	chroma::System s;
	if (!s.load_bios(bios.data(), bios.size())) { fprintf(stderr, "bad bios (%zu bytes)\n", bios.size()); return 1; }
	if (!s.load_cart(cart.data(), cart.size())) { fprintf(stderr, "bad cart\n"); return 1; }
	s.reset();
	size_t samples = 0; int nonblack = 0;
	for (int i = 0; i < frames; i++) {
		s.set_buttons((i % 120) > 100 ? chroma::BTN_A : 0);
		s.run_frame(60); samples += s.audio().size();
	}
	const uint32_t *fb = s.framebuffer();
	for (int i = 0; i < 320 * 240; i++) if ((fb[i] & 0xffffff) != 0) nonblack++;
	printf("frames=%d samples=%zu nonblack_pixels=%d\n", frames, samples, nonblack);
	if (argc > 4) {
		FILE *o = fopen(argv[4], "wb"); fprintf(o, "P6\n320 240\n255\n");
		for (int i = 0; i < 320 * 240; i++) { uint8_t px[3] = { (uint8_t)(fb[i] >> 16), (uint8_t)(fb[i] >> 8), (uint8_t)fb[i] }; fwrite(px, 1, 3, o); }
		fclose(o);
	}
	auto st = s.save_state(); printf("state=%zu bytes, reload=%d\n", st.size(), s.load_state(st));
}
