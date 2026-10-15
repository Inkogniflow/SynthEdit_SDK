#pragma once
#include <set>
#include <string>
#include <cstdio>
#include "mp_sdk_audio.h"
#include "mp_sdk_gui2.h"
#include "Drawing_API.h"

// Calls to methods the harness does not implement are recorded and reported.
inline std::set<std::string>& unimplementedCalls() { static std::set<std::string> s; return s; }
inline void log_call(const char* name) { unimplementedCalls().insert(name); }

inline std::set<std::string>& unknownQueries() { static std::set<std::string> s; return s; }
inline void log_qi(const char* who, const gmpi::MpGuid& g)
{
	char b[200];
	snprintf(b, sizeof(b), "%s ? {%08x-%04x-%04x}", who, (unsigned)g.data1, g.data2, g.data3);
	unknownQueries().insert(b);
}

struct StubRef { int refs = 1; };

#define STUB_UNKNOWN_IMPL \
	int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** r) override { return onQuery(iid, r); } \
	virtual int32_t onQuery(const gmpi::MpGuid& iid, void** r) { log_qi(typeid(*this).name(), iid); *r = nullptr; return gmpi::MP_NOSUPPORT; } \
	int32_t MP_STDCALL addRef() override { return ++refs; } \
	int32_t MP_STDCALL release() override { return --refs; /* leaked on purpose: test harness */ }
