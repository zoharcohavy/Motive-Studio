#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// MotiveScript — a tiny per-sample DSP language, VEX-style.
//
// The user writes the *body* of the per-sample loop; the host runs it once per
// sample. `@s` is the current sample (read/write it — that's the effect).
// Using `@s.l` / `@s.r` anywhere makes the script stereo (one run per frame,
// both channels visible); otherwise it's mono (one run per channel, each
// channel with its own copy of every variable, so filter states stay correct).
//
//   param drive 1 to 25 = 6;          // becomes a slider + automatable
//   x = tanh(@s * drive);             // variables persist across samples
//   @s = x + sample(@t - 0.25) * 0.4; // sample() reads the input's past
//
// Builtins:  @s @s.l @s.r  @t (sec)  @n (sample#)  @srate  @beat  @tempo
// Functions: sin cos tan tanh abs sqrt exp log floor ceil noise()
//            pow(x,y) min max lerp(a,b,t) clamp(x,lo,hi)
//            sample(t) / sample.l(t) / sample.r(t)
// Statements: assignment (= += -= *= /=), if/else, param NAME LO to HI = DEF;
//
// Architecture: single-pass recursive-descent compiler -> stack bytecode ->
// a zero-allocation VM. Compiling happens on the message thread; the audio
// thread only ever runs an already-built Program.
// ============================================================================

namespace motivescript
{

//==============================================================================
enum class Builtin { s, sl, sr, t, n, srate, beat, tempo };

enum class OpCode : uint8_t
{
    pushConst, loadVar, storeVar, loadParam, loadBuiltin, storeBuiltin,
    add, sub, mul, div, mod, neg, logicalNot,
    lt, le, gt, ge, eq, ne, logicalAnd, logicalOr,
    jump, jumpIfZero,
    callFn,        // arg = function id (arity is per-function)
    sampleHist,    // arg = channel: 0=L, 1=R, 2=current (mono)
    end
};

struct Op
{
    OpCode code;
    int32_t arg = 0;
};

enum class Fn { sin, cos, tan, tanh, abs, sqrt, exp, log, floor, ceil,
                noise, pow, min, max, lerp, clamp };

inline int fnArity (Fn f)
{
    switch (f)
    {
        case Fn::noise:                return 0;
        case Fn::pow: case Fn::min:
        case Fn::max:                  return 2;
        case Fn::lerp: case Fn::clamp: return 3;
        default:                       return 1;
    }
}

//==============================================================================
struct Param
{
    std::string name;
    double low = 0.0, high = 1.0, def = 0.0;
};

struct Program
{
    std::vector<Op> ops;
    std::vector<double> consts;
    std::vector<Param> params;
    std::vector<std::string> varNames;
    bool stereo = false;

    static constexpr int maxParams = 8;
};

struct CompileResult
{
    std::shared_ptr<const Program> program;   // null on failure
    std::string error;                        // empty on success
    int errorLine = 0;
};

//==============================================================================
// Everything the VM needs for one sample. The host fills this in per sample.
struct Context
{
    double* vars = nullptr;                 // program->varNames.size() slots
    const double* params = nullptr;         // resolved to their real ranges
    double s[2] { 0.0, 0.0 };               // current sample, L/R (mono uses [0])
    int channel = 0;                        // which channel a mono run is on
    double t = 0.0, n = 0.0, sampleRate = 44100.0, beat = 0.0, tempo = 120.0;

    const float* history[2] { nullptr, nullptr };   // input past, ring buffers
    int historySize = 0, historyWrite = 0;

    uint32_t rngState = 0x9e3779b9;
};

//==============================================================================
class Compiler
{
public:
    static CompileResult compile (const std::string& source)
    {
        Compiler c (source);

        try
        {
            c.parseScript();
        }
        catch (const ScriptError& e)
        {
            return { nullptr, e.message, e.line };
        }

        c.emit (OpCode::end);
        return { std::make_shared<const Program> (std::move (c.program)), "", 0 };
    }

private:
    explicit Compiler (const std::string& s) : src (s) {}

    //==========================================================================
    struct ScriptError
    {
        std::string message;
        int line;
    };

    [[noreturn]] void fail (const std::string& message) const
    {
        throw ScriptError { message, line };
    }

    //==========================================================================
    // lexer: one token of lookahead
    enum class Tok { number, identifier, builtin, keyword, symbol, eof };

    Tok tokType = Tok::eof;
    std::string tokText;
    double tokNumber = 0.0;
    int line = 1;

    const std::string& src;
    size_t pos = 0;

    void skipSpace()
    {
        while (pos < src.size())
        {
            if (src[pos] == '\n') { ++line; ++pos; }
            else if (std::isspace ((unsigned char) src[pos])) ++pos;
            else if (src[pos] == '/' && pos + 1 < src.size() && src[pos + 1] == '/')
                while (pos < src.size() && src[pos] != '\n') ++pos;
            else break;
        }
    }

    static bool isIdentChar (char c)   { return std::isalnum ((unsigned char) c) || c == '_'; }

    // fold a trailing ".l" / ".r" into the name (for @s and sample)
    void maybeFoldChannelSuffix()
    {
        if (pos + 1 < src.size() && src[pos] == '.'
            && (src[pos + 1] == 'l' || src[pos + 1] == 'r')
            && (pos + 2 >= src.size() || ! isIdentChar (src[pos + 2])))
        {
            tokText += { src[pos], src[pos + 1] };
            pos += 2;
        }
    }

    void next()
    {
        skipSpace();

        if (pos >= src.size()) { tokType = Tok::eof; tokText = "<end of script>"; return; }

        const char c = src[pos];

        if (std::isdigit ((unsigned char) c) || (c == '.' && pos + 1 < src.size() && std::isdigit ((unsigned char) src[pos + 1])))
        {
            size_t used = 0;
            tokNumber = std::stod (src.substr (pos), &used);
            pos += used;
            tokType = Tok::number;
            return;
        }

        if (c == '@')
        {
            ++pos;
            tokText.clear();
            while (pos < src.size() && isIdentChar (src[pos])) tokText += src[pos++];
            if (tokText == "s") maybeFoldChannelSuffix();
            tokType = Tok::builtin;
            return;
        }

        if (std::isalpha ((unsigned char) c) || c == '_')
        {
            tokText.clear();
            while (pos < src.size() && isIdentChar (src[pos])) tokText += src[pos++];
            if (tokText == "sample") maybeFoldChannelSuffix();

            tokType = (tokText == "param" || tokText == "to" || tokText == "if" || tokText == "else")
                          ? Tok::keyword : Tok::identifier;
            return;
        }

        // multi-char operators first
        static const char* twoChar[] = { "+=", "-=", "*=", "/=", "==", "!=", "<=", ">=", "&&", "||" };
        for (auto* op : twoChar)
        {
            if (src.compare (pos, 2, op) == 0)
            {
                tokText = op;
                pos += 2;
                tokType = Tok::symbol;
                return;
            }
        }

        tokText = std::string (1, c);
        ++pos;
        tokType = Tok::symbol;
    }

    bool accept (const char* symbol)
    {
        if (tokType == Tok::symbol && tokText == symbol) { next(); return true; }
        return false;
    }

    void expect (const char* symbol)
    {
        if (! accept (symbol))
            fail ("expected '" + std::string (symbol) + "' but found '" + tokText + "'");
    }

    //==========================================================================
    // code emission
    Program program;

    void emit (OpCode code, int32_t arg = 0)    { program.ops.push_back ({ code, arg }); }

    int emitJump (OpCode code)                  { emit (code); return (int) program.ops.size() - 1; }
    void patchJump (int at)                     { program.ops[(size_t) at].arg = (int32_t) program.ops.size(); }

    void emitConst (double value)
    {
        program.consts.push_back (value);
        emit (OpCode::pushConst, (int32_t) program.consts.size() - 1);
    }

    int varSlot (const std::string& name, bool createIfMissing)
    {
        for (size_t i = 0; i < program.varNames.size(); ++i)
            if (program.varNames[i] == name)
                return (int) i;

        if (! createIfMissing)
            fail ("unknown variable '" + name + "'");

        program.varNames.push_back (name);
        return (int) program.varNames.size() - 1;
    }

    int paramIndex (const std::string& name) const
    {
        for (size_t i = 0; i < program.params.size(); ++i)
            if (program.params[i].name == name)
                return (int) i;
        return -1;
    }

    void markStereo()   { program.stereo = true; }

    Builtin builtinFor (const std::string& name)
    {
        if (name == "s")     return Builtin::s;
        if (name == "s.l")   { markStereo(); return Builtin::sl; }
        if (name == "s.r")   { markStereo(); return Builtin::sr; }
        if (name == "t")     return Builtin::t;
        if (name == "n")     return Builtin::n;
        if (name == "srate") return Builtin::srate;
        if (name == "beat")  return Builtin::beat;
        if (name == "tempo") return Builtin::tempo;
        fail ("unknown builtin '@" + name + "'");
    }

    //==========================================================================
    // parsing
    void parseScript()
    {
        next();
        while (tokType != Tok::eof)
            parseStatement();

        // a mono-only reference check: bare @s in a stereo script is ambiguous
        if (program.stereo && usesBareS)
            fail ("this script is stereo (it uses .l/.r) — use @s.l and @s.r instead of @s");
    }

    bool usesBareS = false;

    void parseStatement()
    {
        if (tokType == Tok::keyword && tokText == "param")   return parseParam();
        if (tokType == Tok::keyword && tokText == "if")      return parseIf();

        if (tokType == Tok::builtin)
        {
            const auto b = builtinFor (tokText);

            if (b != Builtin::s && b != Builtin::sl && b != Builtin::sr)
                fail ("'@" + tokText + "' is read-only");

            if (b == Builtin::s) usesBareS = true;

            next();
            parseAssignmentTail (/*isVar*/ false, (int) b);
            return;
        }

        if (tokType == Tok::identifier)
        {
            const auto name = tokText;

            if (paramIndex (name) >= 0)
                fail ("'" + name + "' is a param — params are set by their sliders, not by code");

            next();
            parseAssignmentTail (/*isVar*/ true, varSlot (name, true));
            return;
        }

        fail ("expected a statement but found '" + tokText + "'");
    }

    void parseAssignmentTail (bool isVar, int slot)
    {
        std::string op = tokText;

        if (! (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/="))
            fail ("expected an assignment operator after the left-hand side");

        next();

        if (op != "=")   // compound: load current value first
        {
            if (isVar) emit (OpCode::loadVar, slot);
            else       emit (OpCode::loadBuiltin, slot);
        }

        parseExpression();

        if (op == "+=") emit (OpCode::add);
        if (op == "-=") emit (OpCode::sub);
        if (op == "*=") emit (OpCode::mul);
        if (op == "/=") emit (OpCode::div);

        if (isVar) emit (OpCode::storeVar, slot);
        else       emit (OpCode::storeBuiltin, slot);

        expect (";");
    }

    void parseParam()
    {
        next();   // past 'param'

        if (tokType != Tok::identifier)
            fail ("expected a name after 'param'");

        Param p;
        p.name = tokText;

        if ((int) program.params.size() >= Program::maxParams)
            fail ("too many params (max " + std::to_string (Program::maxParams) + ")");

        if (paramIndex (p.name) >= 0)
            fail ("param '" + p.name + "' declared twice");

        next();
        p.low = parseSignedNumber();

        if (! (tokType == Tok::keyword && tokText == "to"))
            fail ("expected 'to' in param range");

        next();
        p.high = parseSignedNumber();
        expect ("=");
        p.def = parseSignedNumber();
        expect (";");

        program.params.push_back (std::move (p));
    }

    double parseSignedNumber()
    {
        double sign = accept ("-") ? -1.0 : 1.0;

        if (tokType != Tok::number)
            fail ("expected a number");

        const double v = tokNumber * sign;
        next();
        return v;
    }

    void parseIf()
    {
        next();   // past 'if'
        expect ("(");
        parseExpression();
        expect (")");

        const int jumpToElse = emitJump (OpCode::jumpIfZero);
        parseBlock();

        if (tokType == Tok::keyword && tokText == "else")
        {
            next();
            const int jumpToEnd = emitJump (OpCode::jump);
            patchJump (jumpToElse);

            if (tokType == Tok::keyword && tokText == "if") parseIf();   // else if
            else parseBlock();

            patchJump (jumpToEnd);
        }
        else
        {
            patchJump (jumpToElse);
        }
    }

    void parseBlock()
    {
        expect ("{");
        while (tokType != Tok::eof && ! (tokType == Tok::symbol && tokText == "}"))
            parseStatement();
        expect ("}");
    }

    //==========================================================================
    // expressions, classic precedence climbing
    void parseExpression()   { parseTernary(); }

    void parseTernary()
    {
        parseOr();

        if (accept ("?"))
        {
            const int jumpToElse = emitJump (OpCode::jumpIfZero);
            parseExpression();
            expect (":");
            const int jumpToEnd = emitJump (OpCode::jump);
            patchJump (jumpToElse);
            parseExpression();
            patchJump (jumpToEnd);
        }
    }

    void parseOr()
    {
        parseAnd();
        while (accept ("||")) { parseAnd(); emit (OpCode::logicalOr); }
    }

    void parseAnd()
    {
        parseEquality();
        while (accept ("&&")) { parseEquality(); emit (OpCode::logicalAnd); }
    }

    void parseEquality()
    {
        parseRelational();
        for (;;)
        {
            if      (accept ("==")) { parseRelational(); emit (OpCode::eq); }
            else if (accept ("!=")) { parseRelational(); emit (OpCode::ne); }
            else break;
        }
    }

    void parseRelational()
    {
        parseAdditive();
        for (;;)
        {
            if      (accept ("<=")) { parseAdditive(); emit (OpCode::le); }
            else if (accept (">=")) { parseAdditive(); emit (OpCode::ge); }
            else if (accept ("<"))  { parseAdditive(); emit (OpCode::lt); }
            else if (accept (">"))  { parseAdditive(); emit (OpCode::gt); }
            else break;
        }
    }

    void parseAdditive()
    {
        parseMultiplicative();
        for (;;)
        {
            if      (accept ("+")) { parseMultiplicative(); emit (OpCode::add); }
            else if (accept ("-")) { parseMultiplicative(); emit (OpCode::sub); }
            else break;
        }
    }

    void parseMultiplicative()
    {
        parseUnary();
        for (;;)
        {
            if      (accept ("*")) { parseUnary(); emit (OpCode::mul); }
            else if (accept ("/")) { parseUnary(); emit (OpCode::div); }
            else if (accept ("%")) { parseUnary(); emit (OpCode::mod); }
            else break;
        }
    }

    void parseUnary()
    {
        if (accept ("-")) { parseUnary(); emit (OpCode::neg); return; }
        if (accept ("!")) { parseUnary(); emit (OpCode::logicalNot); return; }
        parsePrimary();
    }

    void parsePrimary()
    {
        if (tokType == Tok::number)
        {
            emitConst (tokNumber);
            next();
            return;
        }

        if (accept ("("))
        {
            parseExpression();
            expect (")");
            return;
        }

        if (tokType == Tok::builtin)
        {
            const auto b = builtinFor (tokText);
            if (b == Builtin::s) usesBareS = true;
            emit (OpCode::loadBuiltin, (int) b);
            next();
            return;
        }

        if (tokType == Tok::identifier)
        {
            const auto name = tokText;
            next();

            if (tokType == Tok::symbol && tokText == "(")
                return parseCall (name);

            if (const int p = paramIndex (name); p >= 0)
            {
                emit (OpCode::loadParam, p);
                return;
            }

            emit (OpCode::loadVar, varSlot (name, false));
            return;
        }

        fail ("expected a value but found '" + tokText + "'");
    }

    void parseCall (const std::string& name)
    {
        // sample() reads the input's history ring buffer
        if (name == "sample" || name == "sample.l" || name == "sample.r")
        {
            const int channel = name == "sample.l" ? 0 : name == "sample.r" ? 1 : 2;

            if (channel != 2) markStereo();
            else              usesBareS = true;   // bare sample() is mono-only too

            expect ("(");
            parseExpression();
            expect (")");
            emit (OpCode::sampleHist, channel);
            return;
        }

        static const struct { const char* name; Fn fn; } fns[] =
        {
            { "sin", Fn::sin }, { "cos", Fn::cos }, { "tan", Fn::tan }, { "tanh", Fn::tanh },
            { "abs", Fn::abs }, { "sqrt", Fn::sqrt }, { "exp", Fn::exp }, { "log", Fn::log },
            { "floor", Fn::floor }, { "ceil", Fn::ceil }, { "noise", Fn::noise },
            { "pow", Fn::pow }, { "min", Fn::min }, { "max", Fn::max },
            { "lerp", Fn::lerp }, { "clamp", Fn::clamp },
        };

        for (const auto& f : fns)
        {
            if (name == f.name)
            {
                expect ("(");
                const int arity = fnArity (f.fn);

                for (int i = 0; i < arity; ++i)
                {
                    if (i > 0) expect (",");
                    parseExpression();
                }

                expect (")");
                emit (OpCode::callFn, (int) f.fn);
                return;
            }
        }

        fail ("unknown function '" + name + "'");
    }
};

//==============================================================================
// The VM. One call = one run of the script for one sample (or one channel of
// one sample in mono mode). No allocations, no locks, plain switch dispatch.
inline void run (const Program& p, Context& ctx)
{
    double stack[64];
    int sp = 0;
    size_t ip = 0;

    auto push = [&] (double v) { if (sp < 63) stack[sp++] = v; };
    auto pop  = [&]() -> double { return sp > 0 ? stack[--sp] : 0.0; };

    auto readHistory = [&] (int channel, double atTime) -> double
    {
        if (ctx.history[channel] == nullptr || ctx.historySize <= 0)
            return 0.0;

        auto delay = (int) ((ctx.t - atTime) * ctx.sampleRate + 0.5);
        delay = delay < 1 ? 1 : (delay >= ctx.historySize ? ctx.historySize - 1 : delay);

        int index = ctx.historyWrite - delay;
        if (index < 0) index += ctx.historySize;

        return ctx.history[channel][index];
    };

    while (ip < p.ops.size())
    {
        const auto op = p.ops[ip++];

        switch (op.code)
        {
            case OpCode::pushConst:     push (p.consts[(size_t) op.arg]); break;
            case OpCode::loadVar:       push (ctx.vars[op.arg]); break;
            case OpCode::storeVar:      ctx.vars[op.arg] = pop(); break;
            case OpCode::loadParam:     push (ctx.params[op.arg]); break;

            case OpCode::loadBuiltin:
                switch ((Builtin) op.arg)
                {
                    case Builtin::s:     push (ctx.s[ctx.channel]); break;
                    case Builtin::sl:    push (ctx.s[0]); break;
                    case Builtin::sr:    push (ctx.s[1]); break;
                    case Builtin::t:     push (ctx.t); break;
                    case Builtin::n:     push (ctx.n); break;
                    case Builtin::srate: push (ctx.sampleRate); break;
                    case Builtin::beat:  push (ctx.beat); break;
                    case Builtin::tempo: push (ctx.tempo); break;
                }
                break;

            case OpCode::storeBuiltin:
                switch ((Builtin) op.arg)
                {
                    case Builtin::s:  ctx.s[ctx.channel] = pop(); break;
                    case Builtin::sl: ctx.s[0] = pop(); break;
                    case Builtin::sr: ctx.s[1] = pop(); break;
                    default:          pop(); break;
                }
                break;

            case OpCode::add: { auto b = pop(); push (pop() + b); break; }
            case OpCode::sub: { auto b = pop(); push (pop() - b); break; }
            case OpCode::mul: { auto b = pop(); push (pop() * b); break; }
            case OpCode::div: { auto b = pop(); auto a = pop(); push (b == 0.0 ? 0.0 : a / b); break; }
            case OpCode::mod: { auto b = pop(); auto a = pop(); push (b == 0.0 ? 0.0 : std::fmod (a, b)); break; }
            case OpCode::neg:        push (-pop()); break;
            case OpCode::logicalNot: push (pop() == 0.0 ? 1.0 : 0.0); break;

            case OpCode::lt: { auto b = pop(); push (pop() <  b ? 1.0 : 0.0); break; }
            case OpCode::le: { auto b = pop(); push (pop() <= b ? 1.0 : 0.0); break; }
            case OpCode::gt: { auto b = pop(); push (pop() >  b ? 1.0 : 0.0); break; }
            case OpCode::ge: { auto b = pop(); push (pop() >= b ? 1.0 : 0.0); break; }
            case OpCode::eq: { auto b = pop(); push (pop() == b ? 1.0 : 0.0); break; }
            case OpCode::ne: { auto b = pop(); push (pop() != b ? 1.0 : 0.0); break; }
            case OpCode::logicalAnd: { auto b = pop(); auto a = pop(); push (a != 0.0 && b != 0.0 ? 1.0 : 0.0); break; }
            case OpCode::logicalOr:  { auto b = pop(); auto a = pop(); push (a != 0.0 || b != 0.0 ? 1.0 : 0.0); break; }

            case OpCode::jump:       ip = (size_t) op.arg; break;
            case OpCode::jumpIfZero: if (pop() == 0.0) ip = (size_t) op.arg; break;

            case OpCode::callFn:
                switch ((Fn) op.arg)
                {
                    case Fn::sin:   push (std::sin (pop())); break;
                    case Fn::cos:   push (std::cos (pop())); break;
                    case Fn::tan:   push (std::tan (pop())); break;
                    case Fn::tanh:  push (std::tanh (pop())); break;
                    case Fn::abs:   push (std::abs (pop())); break;
                    case Fn::sqrt:  { auto v = pop(); push (v > 0.0 ? std::sqrt (v) : 0.0); break; }
                    case Fn::exp:   push (std::exp (pop())); break;
                    case Fn::log:   { auto v = pop(); push (v > 0.0 ? std::log (v) : 0.0); break; }
                    case Fn::floor: push (std::floor (pop())); break;
                    case Fn::ceil:  push (std::ceil (pop())); break;

                    case Fn::noise:
                        // xorshift white noise, -1..1
                        ctx.rngState ^= ctx.rngState << 13;
                        ctx.rngState ^= ctx.rngState >> 17;
                        ctx.rngState ^= ctx.rngState << 5;
                        push ((double) (int32_t) ctx.rngState / 2147483648.0);
                        break;

                    case Fn::pow:   { auto b = pop(); push (std::pow (pop(), b)); break; }
                    case Fn::min:   { auto b = pop(); push (std::min (pop(), b)); break; }
                    case Fn::max:   { auto b = pop(); push (std::max (pop(), b)); break; }
                    case Fn::lerp:  { auto t = pop(); auto b = pop(); auto a = pop(); push (a + (b - a) * t); break; }
                    case Fn::clamp: { auto hi = pop(); auto lo = pop(); auto x = pop(); push (x < lo ? lo : (x > hi ? hi : x)); break; }
                }
                break;

            case OpCode::sampleHist:
            {
                const auto atTime = pop();
                const int channel = op.arg == 2 ? ctx.channel : op.arg;
                push (readHistory (channel, atTime));
                break;
            }

            case OpCode::end:
                return;
        }
    }
}

} // namespace motivescript
