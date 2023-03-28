# Tester for C++ symbol shortening in hatari_profile.py

import re

# few of the symbols used by ScummVm 2.6
funcs = [
"famisras10",
"_d_number.isra.111",
"operator delete(void*)",
"operator new(unsigned long)",
"operator new(unsigned long, std::nothrow_t const&)",
"Common::operator+(int, Common::Rational const&)",
"Common::operator-(int, Common::Rational const&)",
"Common::operator*(int, Common::Rational const&)",
"Common::operator/(int, Common::Rational const&)",
"Common::operator==(int, Common::Rational const&)",
"Common::operator!=(int, Common::Rational const&)",
"Common::operator>(int, Common::Rational const&)",
"Common::operator<(int, Common::Rational const&)",
"Common::operator>=(int, Common::Rational const&)",
"Common::operator<=(int, Common::Rational const&)",
"virtual thunk to OSystem_Atari::initBackend()",
"Sci::Script::isValidOffset(unsigned int) const",
"(anonymous namespace)::pool::free(void*) [clone .constprop.4]",
"Sci::MidiDriver_FMTowns::setTimerCallback(void*, void (*)(void*))",
"non-virtual thunk to StdioStream::write(void const*, unsigned int)",
"Scumm::AkosRenderer::byleRLEDecode(Scumm::BaseCostumeRenderer::ByleRLEData&)",
"Common::setErrorOutputFormatter(void (*)(char*, char const*, unsigned long))",
"Common::(anonymous namespace)::BufferedWriteStream::write(void const*, unsigned int)",
"non-virtual thunk to Common::(anonymous namespace)::BufferedSeekableReadStream::seek(long long, int)",
"Sci::GfxAnimate::processViewScaling(Sci::GfxView*, Common::ListInternal::Iterator<Sci::AnimateEntry>)",
"Common::SpanBase<unsigned char, Sci::SciSpan>::validate(unsigned int, int, Common::SpanValidationMode) const [clone .part.167]",
"Common::HashMap<Sci::reg_t, bool, Sci::reg_t_Hash, Common::EqualTo<Sci::reg_t> >::lookup(Sci::reg_t const&) const [clone .isra.42]",
"Common::HashMap<unsigned int, Common::HashMap<unsigned short, Mohawk::Archive::Resource, Common::Hash<unsigned short>, Common::EqualTo<unsigned short> >, Common::Hash<unsigned int>, Common::EqualTo<unsigned int> >::getVal(unsigned int const&) const [clone .isra.23]"
"Common::sort<TwinE::Renderer::RenderCommand*, TwinE::Renderer::depthSortRenderCommands(int)::{lambda(TwinE::Renderer::RenderCommand const&, TwinE::Renderer::RenderCommand const&)#1}>(TwinE::Renderer::RenderCommand*, TwinE::Renderer::depthSortRenderCommands(int)::{lambda(TwinE::Renderer::RenderCommand const&, TwinE::Renderer::RenderCommand const&)#1}, TwinE::Renderer::depthSortRenderCommands(int)::{lambda(TwinE::Renderer::RenderCommand const&, TwinE::Renderer::RenderCommand const&)#1}) [clone .isra.26]",
]

# C symbols
re_isra = re.compile(r"\.isra\.[0-9]+$")

# C++ symbols
re_thunk = re.compile(r"(non-)?virtual ") # ...thunk to
re_clone = re.compile(r" \[clone[^]]+\]") # ...isra
re_args = re.compile(r"[^(+]+::[^(+]+(\(.+\))") # class::method(args)

out = []
for f in funcs:
    n = f

    if not (" " in n or "::" in n):
        # C-symbols
        for r in (re_isra,):
            m = r.search(n)
            if m:
                n = n[:m.start()] + n[m.end():]
        print("'%s' => '%s'" % (f, n))
        continue

    # remove args from method signatures
    m = re_args.search(n)
    if m:
        print(len(n), m.start(1), m.end(1))
        n = n[:m.start(1)] + "()" + n[m.end(1):]

    # shorten namespaces and template args
    n = n.replace("anonymous namespace", "anon ns")
    n = n.replace("unsigned ", "u")

    # remove virtual and clone infos
    for r in (re_thunk, re_clone):
        m = r.search(n)
        if m:
            n = n[:m.start()] + n[m.end():]

    print("'%s' => '%s'" % (f, n))
