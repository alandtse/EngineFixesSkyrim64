#include "../src/fixes/detail/alpha_distance_sort.h"
#include "../src/fixes/detail/alpha_distance_sort_code.h"
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <xmmintrin.h>

namespace policy = Fixes::AlphaDistanceSort::policy;
namespace detail = Fixes::AlphaDistanceSort::detail;

namespace
{
    std::size_t checks{};

    void Check(bool value, const std::source_location& location = std::source_location::current())
    {
        ++checks;
        if (!value)
            throw std::runtime_error("alpha sort check at line " + std::to_string(location.line()));
    }
    struct Pass
    {
        Pass*            passGroupNext{};
        policy::Position centre{};
        float            radius{};
        std::size_t      ordinal{};
    };

    void CheckProjectedGroupFallback()
    {
        struct Bound
        {
            policy::Position centre;
            float            radius;
        } bound{ { 3, 4, 2 }, 0 };
        const policy::Position direction{ 2, 3, 1 };
        for (const bool closest : { false, true }) {
            Xbyak::CodeGenerator native;
            Xbyak::Label         done;
            native.mov(native.rax, reinterpret_cast<std::uintptr_t>(&bound));
            native.mov(native.rdx, reinterpret_cast<std::uintptr_t>(&direction));
            // Preserve the native group's scalar operation order and conditional radius subtraction.
            native.movss(native.xmm0, native.ptr[native.rdx]);
            native.movss(native.xmm2, native.ptr[native.rdx + 4]);
            native.movss(native.xmm1, native.ptr[native.rdx + 8]);
            native.mulss(native.xmm0, native.ptr[native.rax]);
            native.mulss(native.xmm2, native.ptr[native.rax + 4]);
            native.mulss(native.xmm1, native.ptr[native.rax + 8]);
            native.addss(native.xmm2, native.xmm0);
            native.addss(native.xmm2, native.xmm1);
            native.mov(native.r8d, closest);
            native.test(native.r8b, native.r8b);
            native.jz(done);
            native.subss(native.xmm2, native.ptr[native.rax + 12]);
            native.L(done);
            native.movaps(native.xmm0, native.xmm2);
            native.ret();
            native.readyRE();
            for (const auto bits : { 0u, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x7F7FFFFFu,
                     0x7F800000u, 0xFF800000u, 0x7FC00001u, 0xFFC00001u }) {
                volatile auto runtimeBits = bits;
                bound.radius = std::bit_cast<float>(std::uint32_t(runtimeBits));
                const auto expected = std::bit_cast<std::uint32_t>(native.getCode<float (*)()>()());
                const auto actual = std::bit_cast<std::uint32_t>(policy::ProjectedGroupKey(bound.centre, bound.radius, direction, closest));
                Check(actual == expected);
            }
        }
    }

    void CheckPolicy()
    {
        using namespace policy;
        const Position origin{ 16046.195f, -8945.370f, -4524.980f };
        const Position puff{ 41638.699f, -26081.252f, 6552.832f };
        const Position broad{ 36641.836f, -40280.773f, 11265.123f };
        Check(DistanceKey(broad, 27522.049f, origin, 0) > DistanceKey(puff, 7149.146f, origin, 0));
        Check(DistanceKey({ 3, 4, 0 }, 0, { 0, 0, 0 }, 0) == 25);
        Check(DistanceKey({ 3, 4, 0 }, 8, { 0, 0, 0 }, 1) == -9);
        Check(DistanceKey({ 3, 4, 0 }, 2, { 0, 0, 0 }, 1) == 9);
        Check(DistanceKey({ 3, 4, 0 }, 0, { 3, 4, 0 }, 0) == 0);
        Check(DistanceKey({ -4, 0, 3 }, 0, {}, 0) == 25);
        Check(DistanceKey({ 13, -16, 30 }, 0, { 10, -20, 30 }, 0) == 25);
        constexpr std::array<float, 9> signedKeys{ -16, -9, -4, -1, 0, 1, 4, 9, 16 };
        for (std::size_t distance = 0; distance < signedKeys.size(); ++distance) {
            const auto key = DistanceKey({ float(distance), 0, 0 }, 4, {}, 1);
            Check(key == signedKeys[distance]);
            if (distance)
                Check(key > signedKeys[distance - 1]);
        }
        constexpr std::array<float, 6> closestFactors{ -1, 0, 0.5f, 1, 2, 3 };
        constexpr std::array<float, 6> closestKeys{ 49, 25, 16, 9, 1, -1 };
        for (std::size_t i = 0; i < closestFactors.size(); ++i)
            Check(DistanceKey({ 3, 4, 0 }, 2, {}, closestFactors[i]) == closestKeys[i]);
        const float tiny = std::ldexp(1.0f, -70);
        Check(std::bit_cast<std::uint32_t>(DistanceKey({ tiny, 0, 0 }, 0, {}, 0)) == 0);
        Check(std::bit_cast<std::uint32_t>(DistanceKey({}, tiny, {}, 1)) == 0);
        const float huge = std::numeric_limits<float>::max();
        Check(policy::IsFinite(DistanceKey({ huge, huge, huge }, 0, { -huge, 0, 0 }, 0)));
        Check(DistanceKey({ 0, 0, 0 }, huge, { 0, 0, 0 }, 1) > -huge);
        Check(DistanceKey({ std::numeric_limits<float>::quiet_NaN(), 0, 0 }, 0, { 0, 0, 0 }, 0) == 0);
        Check(DistanceKey({ 1, 0, 0 }, -1, { 0, 0, 0 }, 0) == 0);
        for (const auto bits : { 0x7F800000u, 0xFF800000u, 0x7FC00001u, 0xFFC00001u }) {
            volatile std::uint32_t runtimeBits = bits;
            const auto             invalid = std::bit_cast<float>(std::uint32_t(runtimeBits));
            Check(!IsFinite({ invalid, 0, 0 }));
            Check(!IsFinite({ 0, invalid, 0 }));
            Check(!IsFinite({ 0, 0, invalid }));
            Check(DistanceKey({ invalid, 0, 0 }, 1, {}, 0) == 0);
            Check(DistanceKey({ 1, 0, 0 }, 1, { invalid, 0, 0 }, 0) == 0);
            Check(DistanceKey({ 1, 0, 0 }, invalid, {}, 0) == 0);
            Check(DistanceKey({ 1, 0, 0 }, 1, {}, invalid) == 0);
        }

        std::mt19937 random(41);
        for (const std::size_t count : { 0u, 1u, 2u, 3u, 17u, 31u, 32u, 33u, 64u, 786u, 25000u, 65536u, 100000u }) {
            std::vector<Pass>  passes(count);
            std::vector<Pass*> expected;
            for (std::size_t i = 0; i < count; ++i) {
                passes[i] = { i + 1 < count ? &passes[i + 1] : nullptr,
                    { static_cast<float>(random() % 100), 0, 0 }, static_cast<float>(random() % 100), i };
                expected.push_back(&passes[i]);
            }
            const auto key = [](const Pass* pass) { return DistanceKey(pass->centre, pass->radius, { 0, 0, 0 }, 1); };
            std::stable_sort(expected.begin(), expected.end(), [&](const Pass* a, const Pass* b) { return key(a) > key(b); });
            auto* head = count ? passes.data() : nullptr;
            Sort(head, key);
            for (auto* pass : expected) {
                Check(head == pass);
                head = head->passGroupNext;
            }
            Check(head == nullptr);
        }
    }

    void CheckStableTies()
    {
        for (const std::size_t count : { 32u, 33u }) {
            std::vector<Pass>    passes(count);
            constexpr std::array tieBits{ 0u, 0x80000000u, 1u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu };
            for (std::size_t i = 0; i < count; ++i) {
                volatile auto bits = tieBits[i % tieBits.size()];
                passes[i] = { i + 1 < count ? &passes[i + 1] : nullptr, {}, std::bit_cast<float>(std::uint32_t(bits)), i };
            }
            auto* head = passes.data();
            policy::Sort(head, [](const auto* pass) { return pass->radius; });
            for (auto& pass : passes) {
                Check(head == &pass);
                head = head->passGroupNext;
            }
            Check(head == nullptr);
        }
    }

    void CheckFloatOrdering()
    {
        std::mt19937 random(74);
        for (const std::size_t count : { 2u, 17u, 31u, 32u, 33u, 64u, 10000u }) {
            std::vector<Pass>  passes(count);
            std::vector<Pass*> expected;
            for (std::size_t i = 0; i < count; ++i) {
                const auto  bits = std::uint32_t(random());
                const float value = std::bit_cast<float>(bits);
                // Deliberately interleave equal signed zeros with the full finite exponent range.
                const float depth = i % 3 == 0 || !policy::IsFinite(value) ? (i % 2 ? -0.0f : 0.0f) : value;
                passes[i] = { i + 1 < count ? &passes[i + 1] : nullptr, {}, depth, i };
                expected.push_back(&passes[i]);
            }
            std::stable_sort(expected.begin(), expected.end(), [](auto* a, auto* b) {
                const auto referenceKey = [](float value) {
                    return std::fabs(value) < std::numeric_limits<float>::min() ? 0.0f : value;
                };
                return referenceKey(a->radius) > referenceKey(b->radius);
            });
            auto*       head = passes.data();
            std::size_t evaluations = 0;
            policy::Sort(head, [&](auto* pass) { ++evaluations; return pass->radius; });
            Check(evaluations <= 4 * count);
            for (auto* pass : expected) {
                Check(head == pass);
                head = head->passGroupNext;
            }
            Check(head == nullptr);
        }
    }

    void CheckOrderingModes()
    {
        struct Restore
        {
            unsigned int original = _mm_getcsr();
            ~Restore() { _mm_setcsr(original); }
        } restore;
        constexpr unsigned int denormalFlags = (1u << 6) | (1u << 15);
        for (const auto mode : { 0u, denormalFlags }) {
            _mm_setcsr((restore.original & ~denormalFlags) | mode);
            CheckProjectedGroupFallback();
            CheckPolicy();
            CheckStableTies();
            CheckFloatOrdering();
        }
    }

    constexpr bool IsGroup(detail::KeySite site)
    {
        return site == detail::KeySite::Group || site == detail::KeySite::GroupAe;
    }

    struct Invocation
    {
        std::uintptr_t                              stub{};
        std::array<std::uintptr_t, 7>               registers{};
        std::array<std::uintptr_t, 6>               preserved{};  // rsi, rdi, r15, rbp, r13, r14
        std::array<std::array<std::uint32_t, 4>, 6> vectors{};
        float                                       key{};
        std::uint64_t                               flags{};
        std::uintptr_t                              bound{}, cameraOrDirection{}, closest{}, alignment{};
    };

    class ClobberHelper : public Xbyak::CodeGenerator
    {
    public:
        ClobberHelper(Invocation& state, detail::KeySite site) :
            CodeGenerator(512)
        {
            mov(r11, reinterpret_cast<std::uintptr_t>(&state));
            mov(ptr[r11 + offsetof(Invocation, bound)], rcx);
            mov(ptr[r11 + offsetof(Invocation, cameraOrDirection)], rdx);
            if (IsGroup(site))
                mov(ptr[r11 + offsetof(Invocation, closest)], r8);
            else
                movss(ptr[r11 + offsetof(Invocation, closest)], xmm2);
            mov(rax, rsp);
            and_(rax, 15);
            mov(ptr[r11 + offsetof(Invocation, alignment)], rax);
            for (auto reg : { rax, rcx, rdx, r8, r9, r10, r11 })
                mov(reg, 0xBADBAD);
            for (int i = 0; i < 6; ++i)
                pcmpeqd(Xbyak::Xmm(i), Xbyak::Xmm(i));
            mov(eax, std::bit_cast<std::uint32_t>(42.25f));
            movd(xmm0, eax);
            clc();
            ret();
            readyRE();
        }
    };

    class Invoke : public Xbyak::CodeGenerator
    {
    public:
        std::uintptr_t continuation{};
        Invoke(Invocation& state, detail::KeySite site, bool closest) :
            CodeGenerator(2048)
        {
            for (auto reg : { rsi, rdi, r15, r12, rbp, r13, r14, rbx })
                push(reg);
            sub(rsp, 0x38);
            movdqu(ptr[rsp + 0x10], xmm6);
            movdqu(ptr[rsp + 0x20], xmm8);
            mov(r12, reinterpret_cast<std::uintptr_t>(&state));
            for (int i = 0; i < 6; ++i) {
                mov(eax, 0x3F800000u + static_cast<std::uint32_t>(i));
                movd(Xbyak::Xmm(i), eax);
                pshufd(Xbyak::Xmm(i), Xbyak::Xmm(i), 0);
            }
            mov(eax, std::bit_cast<std::uint32_t>(3.0f));
            movd(xmm8, eax);
            if (site == detail::KeySite::Peek)
                movss(xmm3, xmm8);
            if (site == detail::KeySite::GroupAe) {
                mov(rbp, 0x1000);
                mov(r14, 0x2000);
                mov(r13, closest);
                mov(rsi, 0x5000);
                mov(rdi, 0x5001);
                mov(r15, 0x5002);
            } else {
                mov(rsi, 0x1000);
                mov(rdi, 0x2000);
                mov(r15, site == detail::KeySite::Group ? closest : 0x3000);
                mov(rbp, 0x5000);
                mov(r13, 0x5001);
                mov(r14, 0x5002);
            }
            mov(rax, 0x1000 - 0xE4);
            mov(rcx, 0xCC);
            mov(rdx, 0x3000);
            mov(r8, 0x88);
            mov(r9, 0x99);
            mov(r10, 0xAA);
            mov(r11, 0xBB);
            if (site == detail::KeySite::Peek)
                sub(rsp, 8);
            cmp(r11, r11);
            stc();
            jmp(ptr[r12]);
            continuation = reinterpret_cast<std::uintptr_t>(getCurr());
            int index = 0;
            for (auto reg : { rax, rcx, rdx, r8, r9, r10, r11 })
                mov(ptr[r12 + offsetof(Invocation, registers) + index++ * 8], reg);
            index = 0;
            for (auto reg : { rsi, rdi, r15, rbp, r13, r14 })
                mov(ptr[r12 + offsetof(Invocation, preserved) + index++ * 8], reg);
            for (int i = 0; i < 6; ++i)
                movdqu(ptr[r12 + offsetof(Invocation, vectors) + i * 16], Xbyak::Xmm(i));
            movss(ptr[r12 + offsetof(Invocation, key)], site == detail::KeySite::Consume ? xmm6 : xmm2);
            pushfq();
            pop(rax);
            mov(ptr[r12 + offsetof(Invocation, flags)], rax);
            if (site == detail::KeySite::Peek)
                add(rsp, 8);
            movdqu(xmm6, ptr[rsp + 0x10]);
            movdqu(xmm8, ptr[rsp + 0x20]);
            add(rsp, 0x38);
            for (auto reg : { rbx, r14, r13, rbp, r12, r15, rdi, rsi })
                pop(reg);
            ret();
            readyRE();
        }
    };

    void CheckNativeABI()
    {
        using namespace detail;
        constexpr std::array cases{
            std::pair{ KeySite::Group, false }, std::pair{ KeySite::Group, true },
            std::pair{ KeySite::GroupAe, false }, std::pair{ KeySite::GroupAe, true },
            std::pair{ KeySite::Peek, false }, std::pair{ KeySite::Consume, false }
        };
        for (const auto& [site, closest] : cases) {
            Invocation    state;
            std::uint32_t count = 17;
            ClobberHelper helper(state, site);
            Invoke        invoke(state, site, closest);
            KeyCode       code(site, reinterpret_cast<std::uintptr_t>(helper.getCode()), invoke.continuation, reinterpret_cast<std::uintptr_t>(&count));
            state.stub = reinterpret_cast<std::uintptr_t>(code.getCode());
            invoke.getCode<void (*)()>()();
            Check(state.bound == 0x1000);
            Check(state.cameraOrDirection == (IsGroup(site) ? 0x2000u : 0x3000u));
            Check(state.closest == (IsGroup(site) ? std::uint32_t(closest) : std::bit_cast<std::uint32_t>(3.0f)));
            Check(state.alignment == 8);
            Check(state.key == 42.25f);
            constexpr std::uint64_t statusMask = 0x8C5;  // CF, PF, ZF, SF and OF; TEST leaves AF undefined.
            const std::uint64_t     expectedFlags = IsGroup(site) ? (closest ? 0 : 0x44) : 0x45;
            Check((state.flags & statusMask) == expectedFlags);
            const std::array<std::uintptr_t, 7> expected{ site == KeySite::Peek ? 17u : 0x1000u - 0xE4u, 0xCC, 0x3000, 0x88, 0x99, 0xAA, 0xBB };
            Check(state.registers == expected);
            const std::uintptr_t                group = closest ? 1 : 0;
            const std::array<std::uintptr_t, 6> expectedPreserved = site == KeySite::GroupAe ?
                                                                        std::array<std::uintptr_t, 6>{ 0x5000, 0x5001, 0x5002, 0x1000, group, 0x2000 } :
                                                                        std::array<std::uintptr_t, 6>{ 0x1000, 0x2000, site == KeySite::Group ? group : 0x3000, 0x5000, 0x5001, 0x5002 };
            Check(state.preserved == expectedPreserved);
            for (std::size_t i = 0; i < state.vectors.size(); ++i) {
                if (i == 2 && site != KeySite::Consume)
                    continue;
                for (std::size_t lane = 0; lane < 4; ++lane) {
                    const auto value = i == 3 && lane == 0 && site == KeySite::Peek ? std::bit_cast<std::uint32_t>(3.0f) : 0x3F800000u + static_cast<std::uint32_t>(i);
                    Check(state.vectors[i][lane] == value);
                }
            }
        }
    }
}

int main()
{
    try {
        CheckOrderingModes();
        CheckNativeABI();
        std::cout << "PASS: " << checks << " alpha ordering and native key ABI checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
