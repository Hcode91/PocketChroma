// PocketChroma JNI bridge
#include <jni.h>
#include <android/bitmap.h>
#include <mutex>
#include "system.h"

static chroma::System *g_sys = nullptr;
static std::mutex g_lock;

static std::vector<uint8_t> to_vec(JNIEnv *env, jbyteArray a)
{
	jsize n = env->GetArrayLength(a);
	std::vector<uint8_t> v(n);
	env->GetByteArrayRegion(a, 0, n, (jbyte *)v.data());
	return v;
}
static jbyteArray to_arr(JNIEnv *env, const uint8_t *p, size_t n)
{
	jbyteArray a = env->NewByteArray((jsize)n);
	env->SetByteArrayRegion(a, 0, (jsize)n, (const jbyte *)p);
	return a;
}

#define FN(name) Java_com_hcode91_pocketchroma_Native_##name

extern "C" {

JNIEXPORT void JNICALL FN(create)(JNIEnv *, jclass)
{
	std::lock_guard<std::mutex> l(g_lock);
	delete g_sys;
	g_sys = new chroma::System();
}

JNIEXPORT jboolean JNICALL FN(loadBios)(JNIEnv *env, jclass, jbyteArray data)
{
	std::lock_guard<std::mutex> l(g_lock);
	auto v = to_vec(env, data);
	return g_sys && g_sys->load_bios(v.data(), v.size());
}

JNIEXPORT jboolean JNICALL FN(loadCart)(JNIEnv *env, jclass, jbyteArray data)
{
	std::lock_guard<std::mutex> l(g_lock);
	auto v = to_vec(env, data);
	return g_sys && g_sys->load_cart(v.data(), v.size());
}

JNIEXPORT jboolean JNICALL FN(loadSave)(JNIEnv *env, jclass, jbyteArray data)
{
	std::lock_guard<std::mutex> l(g_lock);
	auto v = to_vec(env, data);
	return g_sys && g_sys->load_save(v.data(), v.size());
}

JNIEXPORT void JNICALL FN(reset)(JNIEnv *, jclass)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (g_sys) g_sys->reset();
}

JNIEXPORT void JNICALL FN(setButtons)(JNIEnv *, jclass, jint b)
{
	if (g_sys) g_sys->set_buttons((uint32_t)b);
}

// Runs one frame, copies video into the bitmap, writes audio into `audio`.
// Returns the number of samples written.
JNIEXPORT jint JNICALL FN(runFrame)(JNIEnv *env, jclass, jobject bitmap, jshortArray audio)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (!g_sys) return 0;
	g_sys->run_frame(60);

	void *pixels = nullptr;
	AndroidBitmapInfo info;
	if (AndroidBitmap_getInfo(env, bitmap, &info) == 0 && AndroidBitmap_lockPixels(env, bitmap, &pixels) == 0)
	{
		const uint32_t *src = g_sys->framebuffer();
		for (uint32_t y = 0; y < info.height && y < 240; y++)
		{
			uint32_t *dst = (uint32_t *)((uint8_t *)pixels + y * info.stride);
			for (uint32_t x = 0; x < info.width && x < 320; x++)
			{
				uint32_t c = src[y * 320 + x];
				// ARGB -> ABGR (Android RGBA_8888 in memory)
				dst[x] = (c & 0xff00ff00u) | ((c >> 16) & 0xff) | ((c & 0xff) << 16);
			}
		}
		AndroidBitmap_unlockPixels(env, bitmap);
	}

	const auto &a = g_sys->audio();
	jsize cap = env->GetArrayLength(audio);
	jsize n = std::min<jsize>(cap, (jsize)a.size());
	env->SetShortArrayRegion(audio, 0, n, a.data());
	return n;
}

// Returns the 64KB save area if it changed since the last call, else null.
JNIEXPORT jbyteArray JNICALL FN(takeSaveIfDirty)(JNIEnv *env, jclass)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (!g_sys || !g_sys->save_dirty()) return nullptr;
	return to_arr(env, g_sys->save_data(), chroma::System::SAVE_SIZE);
}

JNIEXPORT jbyteArray JNICALL FN(saveState)(JNIEnv *env, jclass)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (!g_sys) return nullptr;
	auto s = g_sys->save_state();
	return to_arr(env, s.data(), s.size());
}

JNIEXPORT jboolean JNICALL FN(loadState)(JNIEnv *env, jclass, jbyteArray data)
{
	std::lock_guard<std::mutex> l(g_lock);
	auto v = to_vec(env, data);
	return g_sys && g_sys->load_state(v);
}

} // extern "C"
