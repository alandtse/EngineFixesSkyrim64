#pragma once

namespace Fixes::BGSKeywordFormLoadCrash
{
    namespace detail
    {
        inline REL::Relocation<bool(RE::TESFile*, std::uint32_t)> TESFile_SetOffsetChunk{ RELOCATION_ID(13899, 13985) };

        inline SafetyHookInline orig_BGSKeywordForm_Load;

        inline void BGSKeywordForm_Load(RE::BGSKeywordForm* a_self, RE::TESFile* a_file)
        {
            // no file(?)
            if (a_file == nullptr)
                return;

            std::uint32_t currentChunkOffset = a_file->formoffset + a_file->fileOffset + 24;

            std::uint32_t numKeywords = 0;

            a_file->ReadData(&numKeywords, sizeof(numKeywords));

            if (numKeywords > 0) {
                TESFile_SetOffsetChunk(a_file, currentChunkOffset);
                orig_BGSKeywordForm_Load.call(a_self, a_file);
                return;
            }

            // KSIZ=0: only the no-next-subrecord case crashes; original handles everything else.
            if (!a_file->SeekNextSubrecord()) {
                logger::warn("fixing invalid keyword form detected at formID {:X} in file {}"sv, a_file->currentform.formID, a_file->fileName);
                return;
            }

            // Reset and call original — it sets Count_10=0 cleanly, preventing stale keyword state on reload.
            logger::warn("fixing invalid keyword form detected at formID {:X} in file {}"sv, a_file->currentform.formID, a_file->fileName);
            TESFile_SetOffsetChunk(a_file, currentChunkOffset);
            orig_BGSKeywordForm_Load.call(a_self, a_file);
        }

        inline void Install()
        {
            orig_BGSKeywordForm_Load = safetyhook::create_inline(RELOCATION_ID(14147, 14255).address(), BGSKeywordForm_Load);
        }
    }

    inline void Install()
    {
        detail::Install();
        logger::info("installed bgskeywordform load crash fix"sv);
    }
}
