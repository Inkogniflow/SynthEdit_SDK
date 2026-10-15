#pragma once

/*
	SXE Waveshaper - formula engine

	A function is a text formula in the input x (-1..+1 = -5..+5 V) that returns y (-1..+1 = -5..+5 V).

	Variables A, B, C, D are the module's A..D audio inputs. Each input always covers -5..+5 V; the range
	written in the formula says what that span means:

		A(-1.5, 3.3)   -5 V -> -1.5,  0 V -> 0.9,  +5 V -> 3.3   (input is clamped to -5..+5 V first)
		A              same as A(-1, 1)

	The same variable may be used several times with different ranges.

	Syntax
		numbers        1, 0.5, .5, 2e-3
		constants      pi, e
		operators      + - * / % ^ (power, right-assoc), unary -, comparisons < > <= >= == != (give 0/1),
		               && || !, implicit multiplication "2x", "3(x+1)", "x(1-x)"
		functions      sin cos tan asin acos atan atan2(y,x) sinh cosh tanh exp log log2 log10 sqrt
		               abs sign floor ceil round frac mod(a,b) pow(a,b) min(a,b) max(a,b) clamp(v,lo,hi)
		               if(cond,a,b) lerp(a,b,t) step(edge,v)
		               spow(v,p)    sign(v)*|v|^p
		               sat(v)       v/(1+|v|)
		               hard(v)      clamp to -1..1
		               fold(v)      triangle fold back into -1..1
		               wrap(v)      wrap around into -1..1
		               cheb(n,v)    Chebyshev polynomial T_n(v), n = 0..32
		               quant(v,s)   round to steps of 1/s
		               sinc(v)      sin(pi v)/(pi v)

	A function list is separated by ';' or new lines. Each entry may start with a name: "Soft clip: tanh(x*A(1,8))".
	Lines starting with '#' or '//' are comments.

	Evaluation is block-wise: every instruction works on a whole block of samples, so the interpreter
	overhead is paid once per block, not per sample.
*/

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

namespace sxws
{
	constexpr int kNumVars = 4;       // A, B, C, D
	constexpr int kMaxFunctions = 64;
	constexpr int kMaxBlock = 256;    // samples per eval() call
	constexpr int kMaxStack = 32;     // deeper formulas are rejected

	enum class Op : uint8_t
	{
		Const, X, Var,
		Neg, Not,
		Add, Sub, Mul, Div, Mod, Pow,
		Lt, Gt, Le, Ge, Eq, Ne, And, Or,
		Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Exp, Log, Log2, Log10, Sqrt,
		Abs, Sign, Floor, Ceil, Round, Frac, Sat, Hard, Fold, Wrap, Sinc,
		Atan2, Min, Max, Step, Spow, Cheb, Quant,
		Clamp, If, Lerp,
	};

	struct Instr
	{
		Op    op;
		uint8_t var;   // Var: 0..3
		float a;       // Const: value,  Var: centre of the range
		float b;       // Var: half the range
	};

	class Program
	{
	public:
		// Compiles one formula (without the "name:" prefix). On error, ok() is false and the program is "x".
		static Program compile(const std::string& formula);

		bool ok() const { return error_.empty(); }
		const std::string& error() const { return error_; }
		int errorPos() const { return errorPos_; }

		// Bit i set = variable i (A..D) is used.
		unsigned varMask() const { return varMask_; }

		// Range of the first use of variable v (for the display). False if v is not used.
		bool varRange(int v, float& lo, float& hi) const;

		// y[i] = f(x[i], vars) for i < n (n <= kMaxBlock).
		// vars[v] points to n normalised values (-1..+1 = -5..+5 V), may be null for unused variables.
		// Results are not sanitised (may be NaN/inf).
		void eval(const float* x, const float* const* vars, float* y, int n) const;

		// Single value, for the display.
		float evalOne(float x, const float* vars) const;

		const std::vector<Instr>& code() const { return code_; }
		int stackDepth() const { return depth_; }

	private:
		std::vector<Instr> code_;
		int depth_ = 1;
		unsigned varMask_ = 0;
		std::string error_;
		int errorPos_ = -1;
		friend struct Compiler;
	};

	struct Function
	{
		std::string name;     // as written, or a short form of the formula
		std::string formula;  // the formula text
		Program program;
	};

	struct FunctionSet
	{
		std::vector<Function> functions;
		unsigned varMask = 0;  // union of all functions
		uint32_t serial = 0;   // unique per parsed set
	};

	// Splits a list (';' / new line separated, optional "name:" prefix) and compiles every entry.
	// Never returns an empty set: an empty list gives the single function "x".
	std::shared_ptr<const FunctionSet> parseFunctionList(const std::string& text);

	// Built-in function tables.
	enum BuiltIn { Saturation, Folding, Harmonics, Asymmetric, Digital, NumBuiltIns };
	const char* builtInText(int which);
	std::shared_ptr<const FunctionSet> getBuiltIn(int which);
}
