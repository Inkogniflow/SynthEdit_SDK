#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include "./FormulaEngine.h"

namespace sxws
{
namespace
{
	constexpr float kPi = 3.14159265358979f;

	// ------------------------------------------------------------------ scalar helpers (shared by VM and folding)
	inline float fFrac(float v) { return v - floorf(v); }
	inline float fSign(float v) { return v > 0.f ? 1.f : (v < 0.f ? -1.f : 0.f); }
	inline float fHard(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }
	inline float fSat(float v) { return v / (1.f + fabsf(v)); }
	inline float fFold(float v)
	{
		// triangle: 0->0, 1->1, 2->0, 3->-1, 4->0 ...
		float t = (v + 1.f) * 0.25f;
		t -= floorf(t);
		return 1.f - 4.f * fabsf(t - 0.5f);
	}
	inline float fWrap(float v)
	{
		float t = (v + 1.f) * 0.5f;
		t -= floorf(t);
		return t * 2.f - 1.f;
	}
	inline float fSinc(float v)
	{
		const float p = kPi * v;
		return fabsf(p) < 1.0e-5f ? 1.f : sinf(p) / p;
	}
	inline float fMod(float a, float b) { return b != 0.f ? a - b * floorf(a / b) : 0.f; }
	inline float fStep(float edge, float v) { return v >= edge ? 1.f : 0.f; }
	inline float fSpow(float v, float p) { return fSign(v) * powf(fabsf(v), p); }
	inline float fCheb(float n, float v)
	{
		int k = (int)lrintf(n);
		if (k <= 0) return 1.f;
		if (k > 32) k = 32;
		float t0 = 1.f, t1 = v;
		for (int i = 1; i < k; ++i)
		{
			const float t2 = 2.f * v * t1 - t0;
			t0 = t1;
			t1 = t2;
		}
		return t1;
	}
	inline float fQuant(float v, float steps)
	{
		const float s = fabsf(steps);
		return s < 1.0e-6f ? v : roundf(v * s) / s;
	}
	inline float fClamp(float v, float lo, float hi)
	{
		if (lo > hi) std::swap(lo, hi);
		return v < lo ? lo : (v > hi ? hi : v);
	}
	inline float fLerp(float a, float b, float t) { return a + t * (b - a); }

	float apply1(Op op, float a)
	{
		switch (op)
		{
		case Op::Neg:   return -a;
		case Op::Not:   return a == 0.f ? 1.f : 0.f;
		case Op::Sin:   return sinf(a);
		case Op::Cos:   return cosf(a);
		case Op::Tan:   return tanf(a);
		case Op::Asin:  return asinf(a);
		case Op::Acos:  return acosf(a);
		case Op::Atan:  return atanf(a);
		case Op::Sinh:  return sinhf(a);
		case Op::Cosh:  return coshf(a);
		case Op::Tanh:  return tanhf(a);
		case Op::Exp:   return expf(a);
		case Op::Log:   return logf(a);
		case Op::Log2:  return log2f(a);
		case Op::Log10: return log10f(a);
		case Op::Sqrt:  return sqrtf(a);
		case Op::Abs:   return fabsf(a);
		case Op::Sign:  return fSign(a);
		case Op::Floor: return floorf(a);
		case Op::Ceil:  return ceilf(a);
		case Op::Round: return roundf(a);
		case Op::Frac:  return fFrac(a);
		case Op::Sat:   return fSat(a);
		case Op::Hard:  return fHard(a);
		case Op::Fold:  return fFold(a);
		case Op::Wrap:  return fWrap(a);
		case Op::Sinc:  return fSinc(a);
		default:        return 0.f;
		}
	}

	float apply2(Op op, float a, float b)
	{
		switch (op)
		{
		case Op::Add:   return a + b;
		case Op::Sub:   return a - b;
		case Op::Mul:   return a * b;
		case Op::Div:   return a / b;
		case Op::Mod:   return fMod(a, b);
		case Op::Pow:   return powf(a, b);
		case Op::Lt:    return a < b ? 1.f : 0.f;
		case Op::Gt:    return a > b ? 1.f : 0.f;
		case Op::Le:    return a <= b ? 1.f : 0.f;
		case Op::Ge:    return a >= b ? 1.f : 0.f;
		case Op::Eq:    return a == b ? 1.f : 0.f;
		case Op::Ne:    return a != b ? 1.f : 0.f;
		case Op::And:   return (a != 0.f && b != 0.f) ? 1.f : 0.f;
		case Op::Or:    return (a != 0.f || b != 0.f) ? 1.f : 0.f;
		case Op::Atan2: return atan2f(a, b);
		case Op::Min:   return a < b ? a : b;
		case Op::Max:   return a > b ? a : b;
		case Op::Step:  return fStep(a, b);
		case Op::Spow:  return fSpow(a, b);
		case Op::Cheb:  return fCheb(a, b);
		case Op::Quant: return fQuant(a, b);
		default:        return 0.f;
		}
	}

	float apply3(Op op, float a, float b, float c)
	{
		switch (op)
		{
		case Op::Clamp: return fClamp(a, b, c);
		case Op::If:    return a != 0.f ? b : c;
		case Op::Lerp:  return fLerp(a, b, c);
		default:        return 0.f;
		}
	}

	struct FuncDef { const char* name; Op op; int args; };
	const FuncDef kFunctions[] =
	{
		{"sin", Op::Sin, 1}, {"cos", Op::Cos, 1}, {"tan", Op::Tan, 1},
		{"asin", Op::Asin, 1}, {"acos", Op::Acos, 1}, {"atan", Op::Atan, 1}, {"atan2", Op::Atan2, 2},
		{"sinh", Op::Sinh, 1}, {"cosh", Op::Cosh, 1}, {"tanh", Op::Tanh, 1},
		{"exp", Op::Exp, 1}, {"log", Op::Log, 1}, {"ln", Op::Log, 1}, {"log2", Op::Log2, 1}, {"log10", Op::Log10, 1},
		{"sqrt", Op::Sqrt, 1}, {"abs", Op::Abs, 1}, {"sign", Op::Sign, 1}, {"sgn", Op::Sign, 1},
		{"floor", Op::Floor, 1}, {"ceil", Op::Ceil, 1}, {"round", Op::Round, 1}, {"frac", Op::Frac, 1},
		{"sat", Op::Sat, 1}, {"hard", Op::Hard, 1}, {"fold", Op::Fold, 1}, {"wrap", Op::Wrap, 1}, {"sinc", Op::Sinc, 1},
		{"mod", Op::Mod, 2}, {"pow", Op::Pow, 2}, {"min", Op::Min, 2}, {"max", Op::Max, 2},
		{"step", Op::Step, 2}, {"spow", Op::Spow, 2}, {"cheb", Op::Cheb, 2}, {"quant", Op::Quant, 2},
		{"clamp", Op::Clamp, 3}, {"if", Op::If, 3}, {"lerp", Op::Lerp, 3},
	};

	// ------------------------------------------------------------------ AST
	struct Node
	{
		Op op = Op::Const;
		float value = 0.f;   // Const
		int var = 0;         // Var
		float centre = 0.f;  // Var
		float half = 1.f;    // Var
		std::vector<std::unique_ptr<Node>> kids;

		bool isConst() const { return op == Op::Const; }
	};

	std::unique_ptr<Node> makeConst(float v)
	{
		auto n = std::make_unique<Node>();
		n->op = Op::Const;
		n->value = v;
		return n;
	}

	// Folds a node whose children are all constants.
	std::unique_ptr<Node> fold(std::unique_ptr<Node> n)
	{
		for (auto& k : n->kids)
			if (!k->isConst())
				return n;

		switch (n->kids.size())
		{
		case 1: return makeConst(apply1(n->op, n->kids[0]->value));
		case 2: return makeConst(apply2(n->op, n->kids[0]->value, n->kids[1]->value));
		case 3: return makeConst(apply3(n->op, n->kids[0]->value, n->kids[1]->value, n->kids[2]->value));
		default: return n;
		}
	}

	std::string lower(std::string s)
	{
		for (auto& c : s)
			c = (char)tolower((unsigned char)c);
		return s;
	}
}

// ---------------------------------------------------------------------- parser
struct Compiler
{
	enum Tok { End, Num, Ident, LParen, RParen, Comma, Operator, Bad };

	const std::string& s;
	size_t pos = 0;
	Tok tok = End;
	std::string text;   // identifier / operator text
	float number = 0.f;
	size_t tokStart = 0;
	std::string error;
	int errorPos = -1;

	explicit Compiler(const std::string& src) : s(src) { next(); }

	void fail(const std::string& msg)
	{
		if (error.empty())
		{
			error = msg;
			errorPos = (int)tokStart;
		}
	}

	void next()
	{
		while (pos < s.size() && isspace((unsigned char)s[pos]))
			++pos;
		tokStart = pos;
		text.clear();

		if (pos >= s.size()) { tok = End; return; }

		const char c = s[pos];
		if (isdigit((unsigned char)c) || (c == '.' && pos + 1 < s.size() && isdigit((unsigned char)s[pos + 1])))
		{
			// manual parse: locale independent, and "2e" followed by a letter is not an exponent
			size_t p = pos;
			while (p < s.size() && isdigit((unsigned char)s[p])) ++p;
			if (p < s.size() && s[p] == '.')
			{
				++p;
				while (p < s.size() && isdigit((unsigned char)s[p])) ++p;
			}
			size_t mantEnd = p;
			if (p < s.size() && (s[p] == 'e' || s[p] == 'E'))
			{
				size_t q = p + 1;
				if (q < s.size() && (s[q] == '+' || s[q] == '-')) ++q;
				if (q < s.size() && isdigit((unsigned char)s[q]))
				{
					while (q < s.size() && isdigit((unsigned char)s[q])) ++q;
					p = q;
				}
			}
			(void)mantEnd;

			// digits -> value without strtod (locale may use ',')
			double mant = 0.0, scale = 1.0;
			bool frac = false;
			size_t i = pos;
			for (; i < p && s[i] != 'e' && s[i] != 'E'; ++i)
			{
				if (s[i] == '.') { frac = true; continue; }
				mant = mant * 10.0 + (s[i] - '0');
				if (frac) scale *= 10.0;
			}
			double value = mant / scale;
			if (i < p) // exponent
			{
				++i;
				int sign = 1;
				if (s[i] == '+') ++i;
				else if (s[i] == '-') { sign = -1; ++i; }
				int ex = 0;
				for (; i < p; ++i)
					ex = (std::min)(ex * 10 + (s[i] - '0'), 400);
				value *= pow(10.0, sign * ex);
			}
			number = (float)value;
			pos = p;
			tok = Num;
			return;
		}
		if (isalpha((unsigned char)c) || c == '_')
		{
			size_t p = pos;
			while (p < s.size() && (isalnum((unsigned char)s[p]) || s[p] == '_')) ++p;
			text = s.substr(pos, p - pos);
			pos = p;
			tok = Ident;
			return;
		}
		++pos;
		switch (c)
		{
		case '(': tok = LParen; return;
		case ')': tok = RParen; return;
		case ',': tok = Comma; return;
		case '+': case '-': case '*': case '/': case '%': case '^':
			text = c; tok = Operator; return;
		case '<': case '>': case '=': case '!':
			text = c;
			if (pos < s.size() && s[pos] == '=') { text += '='; ++pos; }
			if (text == "=") text = "=="; // tolerate a single '='
			tok = Operator;
			return;
		case '&': case '|':
			text = c;
			if (pos < s.size() && s[pos] == c) ++pos;
			text += c;
			tok = Operator;
			return;
		default:
			text = c;
			tok = Bad;
			return;
		}
	}

	bool isOp(const char* o) const { return tok == Operator && text == o; }

	std::unique_ptr<Node> binary(Op op, std::unique_ptr<Node> a, std::unique_ptr<Node> b)
	{
		auto n = std::make_unique<Node>();
		n->op = op;
		n->kids.push_back(std::move(a));
		n->kids.push_back(std::move(b));
		return fold(std::move(n));
	}

	std::unique_ptr<Node> parseExpr() { return parseOr(); }

	std::unique_ptr<Node> parseOr()
	{
		auto n = parseAnd();
		while (isOp("||")) { next(); n = binary(Op::Or, std::move(n), parseAnd()); }
		return n;
	}
	std::unique_ptr<Node> parseAnd()
	{
		auto n = parseCmp();
		while (isOp("&&")) { next(); n = binary(Op::And, std::move(n), parseCmp()); }
		return n;
	}
	std::unique_ptr<Node> parseCmp()
	{
		auto n = parseAdd();
		for (;;)
		{
			Op op;
			if (isOp("<")) op = Op::Lt;
			else if (isOp(">")) op = Op::Gt;
			else if (isOp("<=")) op = Op::Le;
			else if (isOp(">=")) op = Op::Ge;
			else if (isOp("==")) op = Op::Eq;
			else if (isOp("!=")) op = Op::Ne;
			else return n;
			next();
			n = binary(op, std::move(n), parseAdd());
		}
	}
	std::unique_ptr<Node> parseAdd()
	{
		auto n = parseMul();
		for (;;)
		{
			if (isOp("+")) { next(); n = binary(Op::Add, std::move(n), parseMul()); }
			else if (isOp("-")) { next(); n = binary(Op::Sub, std::move(n), parseMul()); }
			else return n;
		}
	}
	std::unique_ptr<Node> parseMul()
	{
		auto n = parseUnary();
		for (;;)
		{
			if (isOp("*")) { next(); n = binary(Op::Mul, std::move(n), parseUnary()); }
			else if (isOp("/")) { next(); n = binary(Op::Div, std::move(n), parseUnary()); }
			else if (isOp("%")) { next(); n = binary(Op::Mod, std::move(n), parseUnary()); }
			else if ((tok == Ident || tok == LParen) && error.empty()) { n = binary(Op::Mul, std::move(n), parseUnary()); } // implicit "2x", "x(1-x)"
			else return n;
		}
	}
	std::unique_ptr<Node> parseUnary()
	{
		if (isOp("-"))
		{
			next();
			auto n = std::make_unique<Node>();
			n->op = Op::Neg;
			n->kids.push_back(parseUnary());
			return fold(std::move(n));
		}
		if (isOp("+")) { next(); return parseUnary(); }
		if (isOp("!"))
		{
			next();
			auto n = std::make_unique<Node>();
			n->op = Op::Not;
			n->kids.push_back(parseUnary());
			return fold(std::move(n));
		}
		return parsePower();
	}
	std::unique_ptr<Node> parsePower()
	{
		auto n = parsePrimary();
		if (isOp("^"))
		{
			next();
			n = binary(Op::Pow, std::move(n), parseUnary()); // right-assoc, allows 2^-x
		}
		return n;
	}

	bool expect(Tok t, const char* what)
	{
		if (tok == t) { next(); return true; }
		fail(std::string("expected ") + what);
		return false;
	}

	std::unique_ptr<Node> parsePrimary()
	{
		if (!error.empty())
			return makeConst(0.f);

		if (tok == Num)
		{
			const float v = number;
			next();
			return makeConst(v);
		}
		if (tok == LParen)
		{
			next();
			auto n = parseExpr();
			expect(RParen, "')'");
			return n;
		}
		if (tok == Ident)
		{
			const std::string name = lower(text);
			next();

			if (name == "x")
			{
				auto n = std::make_unique<Node>();
				n->op = Op::X;
				return n;
			}
			if (name == "pi") return makeConst(kPi);
			if (name == "e")  return makeConst(2.71828182846f);

			if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'd')
			{
				auto n = std::make_unique<Node>();
				n->op = Op::Var;
				n->var = name[0] - 'a';
				float lo = -1.f, hi = 1.f;
				if (tok == LParen)
				{
					// A(min, max): constant range
					next();
					auto a = parseExpr();
					if (!expect(Comma, "',' in range, e.g. A(-1.5, 3.3)")) return makeConst(0.f);
					auto b = parseExpr();
					if (!expect(RParen, "')'")) return makeConst(0.f);
					if (!a->isConst() || !b->isConst())
					{
						fail("range of " + std::string(1, (char)toupper(name[0])) + " must be constant");
						return makeConst(0.f);
					}
					lo = a->value;
					hi = b->value;
				}
				n->centre = 0.5f * (lo + hi);
				n->half = 0.5f * (hi - lo);
				if (n->half == 0.f)
					return makeConst(n->centre);
				return n;
			}

			for (const auto& f : kFunctions)
			{
				if (name != f.name)
					continue;

				auto n = std::make_unique<Node>();
				n->op = f.op;
				if (!expect(LParen, (std::string("'(' after ") + f.name).c_str()))
					return makeConst(0.f);
				for (int i = 0; i < f.args; ++i)
				{
					if (i > 0 && !expect(Comma, (std::string("',' - ") + f.name + " takes " + std::to_string(f.args) + " arguments").c_str()))
						return makeConst(0.f);
					n->kids.push_back(parseExpr());
				}
				if (!expect(RParen, (std::string("')' - ") + f.name + " takes " + std::to_string(f.args) + (f.args == 1 ? " argument" : " arguments")).c_str()))
					return makeConst(0.f);
				return fold(std::move(n));
			}

			fail("unknown name '" + name + "'");
			return makeConst(0.f);
		}

		if (tok == End) fail("unexpected end");
		else if (tok == RParen) fail("unexpected ')'");
		else if (tok == Comma) fail("unexpected ','");
		else fail("unexpected '" + text + "'");
		return makeConst(0.f);
	}

	// ------------------------------------------------------------------ code generation
	void emit(const Node& n, Program& p, int depth)
	{
		for (size_t i = 0; i < n.kids.size(); ++i)
			emit(*n.kids[i], p, depth + (int)i);

		Instr in{};
		in.op = n.op;
		if (n.op == Op::Const) in.a = n.value;
		if (n.op == Op::Var)
		{
			in.var = (uint8_t)n.var;
			in.a = n.centre;
			in.b = n.half;
			p.varMask_ |= 1u << n.var;
		}
		p.code_.push_back(in);

		const int used = depth + 1;
		if (used > p.depth_) p.depth_ = used;
	}
};

Program Program::compile(const std::string& formula)
{
	Program p;
	Compiler c(formula);

	std::unique_ptr<Node> root;
	if (c.tok == Compiler::End)
		c.fail("empty formula");
	else
		root = c.parseExpr();

	if (c.error.empty() && c.tok != Compiler::End)
	{
		if (c.tok == Compiler::RParen) c.fail("unexpected ')'");
		else c.fail("unexpected '" + (c.tok == Compiler::Num ? std::string("number") : c.text) + "'");
	}

	if (c.error.empty())
	{
		c.emit(*root, p, 0);
		if (p.depth_ > kMaxStack)
			c.fail("formula too deeply nested");
	}

	if (!c.error.empty())
	{
		Program fallback;
		fallback.code_.push_back(Instr{ Op::X, 0, 0.f, 0.f });
		fallback.error_ = c.error;
		fallback.errorPos_ = c.errorPos;
		return fallback;
	}
	return p;
}

// ---------------------------------------------------------------------- VM
namespace
{
	template <typename F>
	inline void map1(float* a, int n, F f) { for (int i = 0; i < n; ++i) a[i] = f(a[i]); }
	template <typename F>
	inline void map2(float* a, const float* b, int n, F f) { for (int i = 0; i < n; ++i) a[i] = f(a[i], b[i]); }
}

void Program::eval(const float* x, const float* const* vars, float* y, int n) const
{
	if (n <= 0)
		return;
	if (n > kMaxBlock)
	{
		for (int done = 0; done < n; done += kMaxBlock)
		{
			const float* sub[kNumVars];
			for (int v = 0; v < kNumVars; ++v)
				sub[v] = vars && vars[v] ? vars[v] + done : nullptr;
			eval(x + done, sub, y + done, (std::min)(kMaxBlock, n - done));
		}
		return;
	}

	alignas(16) float stack[kMaxStack][kMaxBlock];
	int sp = -1;

	for (const Instr& in : code_)
	{
		switch (in.op)
		{
		case Op::Const:
		{
			float* d = stack[++sp];
			for (int i = 0; i < n; ++i) d[i] = in.a;
			break;
		}
		case Op::X:
			memcpy(stack[++sp], x, sizeof(float) * n);
			break;
		case Op::Var:
		{
			float* d = stack[++sp];
			const float* src = vars ? vars[in.var] : nullptr;
			if (src)
				for (int i = 0; i < n; ++i) d[i] = in.a + in.b * src[i];
			else
				for (int i = 0; i < n; ++i) d[i] = in.a;
			break;
		}

		// unary
		case Op::Neg:   map1(stack[sp], n, [](float a) { return -a; }); break;
		case Op::Not:   map1(stack[sp], n, [](float a) { return a == 0.f ? 1.f : 0.f; }); break;
		case Op::Sin:   map1(stack[sp], n, [](float a) { return sinf(a); }); break;
		case Op::Cos:   map1(stack[sp], n, [](float a) { return cosf(a); }); break;
		case Op::Tan:   map1(stack[sp], n, [](float a) { return tanf(a); }); break;
		case Op::Asin:  map1(stack[sp], n, [](float a) { return asinf(a); }); break;
		case Op::Acos:  map1(stack[sp], n, [](float a) { return acosf(a); }); break;
		case Op::Atan:  map1(stack[sp], n, [](float a) { return atanf(a); }); break;
		case Op::Sinh:  map1(stack[sp], n, [](float a) { return sinhf(a); }); break;
		case Op::Cosh:  map1(stack[sp], n, [](float a) { return coshf(a); }); break;
		case Op::Tanh:  map1(stack[sp], n, [](float a) { return tanhf(a); }); break;
		case Op::Exp:   map1(stack[sp], n, [](float a) { return expf(a); }); break;
		case Op::Log:   map1(stack[sp], n, [](float a) { return logf(a); }); break;
		case Op::Log2:  map1(stack[sp], n, [](float a) { return log2f(a); }); break;
		case Op::Log10: map1(stack[sp], n, [](float a) { return log10f(a); }); break;
		case Op::Sqrt:  map1(stack[sp], n, [](float a) { return sqrtf(a); }); break;
		case Op::Abs:   map1(stack[sp], n, [](float a) { return fabsf(a); }); break;
		case Op::Sign:  map1(stack[sp], n, fSign); break;
		case Op::Floor: map1(stack[sp], n, [](float a) { return floorf(a); }); break;
		case Op::Ceil:  map1(stack[sp], n, [](float a) { return ceilf(a); }); break;
		case Op::Round: map1(stack[sp], n, [](float a) { return roundf(a); }); break;
		case Op::Frac:  map1(stack[sp], n, fFrac); break;
		case Op::Sat:   map1(stack[sp], n, fSat); break;
		case Op::Hard:  map1(stack[sp], n, fHard); break;
		case Op::Fold:  map1(stack[sp], n, fFold); break;
		case Op::Wrap:  map1(stack[sp], n, fWrap); break;
		case Op::Sinc:  map1(stack[sp], n, fSinc); break;

		// binary: a = stack[sp-1], b = stack[sp]
		case Op::Add: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a + b; }); break;
		case Op::Sub: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a - b; }); break;
		case Op::Mul: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a * b; }); break;
		case Op::Div: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a / b; }); break;
		case Op::Mod: --sp; map2(stack[sp], stack[sp + 1], n, fMod); break;
		case Op::Pow: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return powf(a, b); }); break;
		case Op::Lt:  --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a < b ? 1.f : 0.f; }); break;
		case Op::Gt:  --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a > b ? 1.f : 0.f; }); break;
		case Op::Le:  --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a <= b ? 1.f : 0.f; }); break;
		case Op::Ge:  --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a >= b ? 1.f : 0.f; }); break;
		case Op::Eq:  --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a == b ? 1.f : 0.f; }); break;
		case Op::Ne:  --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a != b ? 1.f : 0.f; }); break;
		case Op::And: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return (a != 0.f && b != 0.f) ? 1.f : 0.f; }); break;
		case Op::Or:  --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return (a != 0.f || b != 0.f) ? 1.f : 0.f; }); break;
		case Op::Atan2: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return atan2f(a, b); }); break;
		case Op::Min: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a < b ? a : b; }); break;
		case Op::Max: --sp; map2(stack[sp], stack[sp + 1], n, [](float a, float b) { return a > b ? a : b; }); break;
		case Op::Step:  --sp; map2(stack[sp], stack[sp + 1], n, fStep); break;
		case Op::Spow:  --sp; map2(stack[sp], stack[sp + 1], n, fSpow); break;
		case Op::Cheb:  --sp; map2(stack[sp], stack[sp + 1], n, fCheb); break;
		case Op::Quant: --sp; map2(stack[sp], stack[sp + 1], n, fQuant); break;

		// ternary: a = stack[sp-2], b = stack[sp-1], c = stack[sp]
		case Op::Clamp:
		{
			sp -= 2;
			float* a = stack[sp]; const float* b = stack[sp + 1]; const float* c = stack[sp + 2];
			for (int i = 0; i < n; ++i) a[i] = fClamp(a[i], b[i], c[i]);
			break;
		}
		case Op::If:
		{
			sp -= 2;
			float* a = stack[sp]; const float* b = stack[sp + 1]; const float* c = stack[sp + 2];
			for (int i = 0; i < n; ++i) a[i] = a[i] != 0.f ? b[i] : c[i];
			break;
		}
		case Op::Lerp:
		{
			sp -= 2;
			float* a = stack[sp]; const float* b = stack[sp + 1]; const float* c = stack[sp + 2];
			for (int i = 0; i < n; ++i) a[i] = fLerp(a[i], b[i], c[i]);
			break;
		}
		}
	}

	memcpy(y, stack[0], sizeof(float) * n);
}

bool Program::varRange(int v, float& lo, float& hi) const
{
	for (const Instr& in : code_)
	{
		if (in.op == Op::Var && in.var == v)
		{
			lo = in.a - in.b;
			hi = in.a + in.b;
			return true;
		}
	}
	return false;
}

float Program::evalOne(float x, const float* vars) const
{
	float v[kNumVars];
	const float* pv[kNumVars];
	for (int i = 0; i < kNumVars; ++i)
	{
		v[i] = vars ? vars[i] : 0.f;
		pv[i] = &v[i];
	}
	float y = 0.f;
	eval(&x, pv, &y, 1);
	return y;
}

// ---------------------------------------------------------------------- lists
namespace
{
	std::string trim(const std::string& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && isspace((unsigned char)s[a])) ++a;
		while (b > a && isspace((unsigned char)s[b - 1])) --b;
		return s.substr(a, b - a);
	}

	std::atomic<uint32_t> g_serial{ 1 };
}

std::shared_ptr<const FunctionSet> parseFunctionList(const std::string& text)
{
	auto set = std::make_shared<FunctionSet>();

	std::string current;
	auto flush = [&]()
	{
		std::string entry = trim(current);
		current.clear();
		if (entry.empty() || entry[0] == '#' || entry.compare(0, 2, "//") == 0)
			return;
		if ((int)set->functions.size() >= kMaxFunctions)
			return;

		Function f;
		const size_t colon = entry.find(':');
		if (colon != std::string::npos)
		{
			f.name = trim(entry.substr(0, colon));
			f.formula = trim(entry.substr(colon + 1));
		}
		else
		{
			f.formula = entry;
		}
		if (f.name.empty())
			f.name = f.formula.size() > 28 ? f.formula.substr(0, 26) + "..." : f.formula;

		f.program = Program::compile(f.formula);
		set->varMask |= f.program.varMask();
		set->functions.push_back(std::move(f));
	};

	for (char c : text)
	{
		if (c == ';' || c == '\n' || c == '\r')
			flush();
		else
			current += c;
	}
	flush();

	if (set->functions.empty())
	{
		Function f;
		f.name = "Linear";
		f.formula = "x";
		f.program = Program::compile("x");
		set->functions.push_back(std::move(f));
	}

	set->serial = g_serial++;
	return set;
}

// ---------------------------------------------------------------------- built-in tables
const char* builtInText(int which)
{
	switch (which)
	{
	case Saturation:
		return
			"Clean: x\n"
			"Soft Clip: tanh(x*A(1,9))\n"
			"Arctan: atan(x*A(1,9))*2/pi\n"
			"Rational: sat(x*A(1,9))*(1+1/A(1,9))\n"
			"Cubic: 1.5*hard(x*A(1,3)) - 0.5*hard(x*A(1,3))^3\n"
			"Exp Clip: sign(x)*(1-exp(-abs(x*A(1,9))))\n"
			"Tube: tanh(x*A(1,9)+B(-0.7,0.7)) - tanh(B(-0.7,0.7))\n"
			"Hard Clip: hard(x*A(1,9))";
	case Folding:
		return
			"Clean: x\n"
			"Sine Fold: sin(x*A(1,7)*pi/2)\n"
			"Triangle Fold: fold(x*A(1,7))\n"
			"Biased Fold: fold(x*A(1,7)+B(-1,1))\n"
			"Soft Fold: tanh(C(1,4)*sin(x*A(1,7)*pi/2))/tanh(C(1,4))\n"
			"Double Fold: fold(sin(x*A(1,5)*pi/2)*B(1,3))\n"
			"Wrap: wrap(x*A(1,4))\n"
			"Ring Fold: sin(x*A(1,7)*pi/2)*cos(x*B(0,8)*pi/2)";
	case Harmonics:
		return
			"Clean: x\n"
			"2nd: lerp(x, cheb(2,hard(x)), A(0,1))\n"
			"3rd: lerp(x, cheb(3,hard(x)), A(0,1))\n"
			"4th: lerp(x, cheb(4,hard(x)), A(0,1))\n"
			"5th: lerp(x, cheb(5,hard(x)), A(0,1))\n"
			"Blend 2-5: (x + A(0,1)*cheb(2,hard(x)) + B(0,1)*cheb(3,hard(x)) + C(0,1)*cheb(4,hard(x)) + D(0,1)*cheb(5,hard(x))) / (1+A(0,1)+B(0,1)+C(0,1)+D(0,1))\n"
			"Odd 3-7: (x + A(0,1)*cheb(3,hard(x)) + A(0,1)^2*cheb(5,hard(x)) + A(0,1)^3*cheb(7,hard(x))) / (1+A(0,1)+A(0,1)^2+A(0,1)^3)\n"
			"Cheb N: cheb(A(1,16),hard(x))";
	case Asymmetric:
		return
			"Clean: x\n"
			"Diode: if(x>0, tanh(x*A(1,9))/tanh(A(1,9)), x*B(0,1))\n"
			"Asym Tanh: tanh(x*A(1,9)*if(x>0, 1, B(0.2,1)))\n"
			"Half Wave: lerp(x, max(x,0)*2-0.5, A(0,1))\n"
			"Full Wave: lerp(x, abs(x)*2-1, A(0,1))\n"
			"Power: spow(x, A(0.2,3))\n"
			"Exponential: (exp(x*A(0.5,4))-1)/(exp(A(0.5,4))-1)\n"
			"Bias Clip: hard(x*A(1,5)+B(-1,1)) - hard(B(-1,1))";
	case Digital:
		return
			"Clean: x\n"
			"Bitcrush: quant(x, 2^A(1,8))\n"
			"Steps: quant(x, A(1,12))\n"
			"Sine Steps: sin(quant(x,A(1,12))*pi/2)\n"
			"Ripple: x + A(0,0.3)*sin(x*B(4,40)*pi)\n"
			"Sinc: sinc(x*A(1,8))\n"
			"Square: sign(x)*hard(abs(x)*A(1,50))\n"
			"Chaos: sin(x*A(1,20)*pi + B(0,4)*sin(x*C(1,30)*pi))";
	default:
		return "x";
	}
}

std::shared_ptr<const FunctionSet> getBuiltIn(int which)
{
	static std::mutex lock;
	static std::shared_ptr<const FunctionSet> cache[NumBuiltIns];

	if (which < 0 || which >= NumBuiltIns)
		which = 0;

	std::lock_guard<std::mutex> guard(lock);
	if (!cache[which])
		cache[which] = parseFunctionList(builtInText(which));
	return cache[which];
}
}
