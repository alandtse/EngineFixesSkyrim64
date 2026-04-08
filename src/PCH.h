#pragma once

#include <string_view>

#include <RE/Skyrim.h>
#include <REX/REX/INI.h>
#include <REX/REX/TOML.h>
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>
#include <SKSE/SKSE.h>

#define WIN32_LEAN_AND_MEAN

#define NOGDICAPMASKS
#define NOVIRTUALKEYCODES
// define NOWINMESSAGES
#define NOWINSTYLES
#define NOSYSMETRICS
#define NOMENUS
#define NOICONS
#define NOKEYSTATES
#define NOSYSCOMMANDS
#define NORASTEROPS
#define NOSHOWWINDOW
#define OEMRESOURCE
#define NOATOM
#define NOCLIPBOARD
#define NOCOLOR
// #define NOCTLMGR
#define NODRAWTEXT
#define NOGDI
#define NOKERNEL
// #define NOUSER
// #define NONLS
// #define NOMB
#define NOMEMMGR
#define NOMETAFILE
#define NOMINMAX
// #define NOMSG
#define NOOPENFILE
#define NOSCROLL
#define NOSERVICE
#define NOSOUND
#define NOTEXTMETRIC
#define NOWH
#define NOWINOFFSETS
#define NOCOMM
#define NOKANJI
#define NOHELP
#define NOPROFILER
#define NODEFERWINDOWPOS
#define NOMCX

#include "windows.h"

#include <ShlObj.h>

#include <safetyhook.hpp>
#include <tbb/concurrent_hash_map.h>
#include <xbyak/xbyak.h>

namespace logger = SKSE::log;

using namespace std::literals;

#include "settings.h"

#include "Version.h"

// VAR_NUM(se, ae)       → VR uses SE value (REL::Relocate 2-arg: se/vr vs ae)
// VAR_NUM(se, ae, vr)   → explicit VR offset (REL::Relocate 3-arg)
#define VAR_NUM_2(se, ae)      REL::Relocate((se), (ae))
#define VAR_NUM_3(se, ae, vr)  REL::Relocate((se), (ae), (vr))
#define VAR_NUM_PICK(_1, _2, _3, NAME, ...) NAME
#define VAR_NUM(...) VAR_NUM_PICK(__VA_ARGS__, VAR_NUM_3, VAR_NUM_2, ~)(__VA_ARGS__)