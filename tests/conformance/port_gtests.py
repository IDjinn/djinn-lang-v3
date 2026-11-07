#!/usr/bin/env python3
"""Evaluates every gtest case against djinnc, writes TRIAGE.md and ports the cases marked Portar.

usage: port_gtests.py <djinnc> <runtime-lib> [--runtime-dir runtime] [--std-dir std]
Run it from the repository root, with the LLVM bin directories on PATH.
"""
import argparse
import collections
import concurrent.futures
import glob
import os
import re
import subprocess
import tempfile
import textwrap

SUITES = "tests/conformance/src/suites"
HEADER = "namespace conformance;\n\nimport testing;\nimport djinnc::testing;\nimport djinnc;\n"
INTERNAL_FILES = {
    "tests/compiler/mangler.cpp": "testa o mangler do stage0",
    "tests/compiler/diagnostics.cpp": "testa a formatação do DiagnosticEngine do stage0",
}
REWRITE_FILES = {
    "tests/compile_time/constexpr.cpp": "usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn",
    "tests/compiler/bootstrap_subset.cpp": "compila vários arquivos pela API C++; vira um caso multiarquivo",
}
SUPERSEDED = {
    "Numbers.CompileTimeOverflowCheck": "aceitava literais fora da faixa, que agora são erro; coberto por `errors/literal_out_of_range`",
}
WRONG_REASON = {
    "Overflow.TrappedRuntimeOverflowAborts": "modos de overflow",
    "Overflow.CompoundAssignmentTrappedAborts": "modos de overflow",
    "Overflow.UnaryNegateTrappedAborts": "modos de overflow",
    "Contracts.ContractCallSiteRequiresTry": "contratos (`require`/`ensure`)",
    "Contracts.RequireConstantViolationIsCompileError": "contratos (`require`/`ensure`)",
    "Contracts.EnforcementStrictRequiresTryEverywhere": "contratos (`require`/`ensure`)",
    "NonZero.CompileTimeViolationForRequireStillWorks": "contratos (`require`/`ensure`)",
    "Overflow.FloatTypeSuffixIsRejected": "modos de overflow",
    "NonZero.RejectsSuffixOnFloatType": "modos de overflow",
    "ErrorHandling.MissingTryDiagnostic": "tipos de erro em `throws`",
    "ErrorHandling.ThrowOutsideThrowsDiagnostic": "tipos de erro em `throws`",
    "ErrorHandling.ThrowsTypeMismatchDiagnostic": "tipos de erro em `throws`",
    "ErrorHandling.TryWithoutFallbackOutsideThrowsDiagnostic": "tipos de erro em `throws`",
    "ErrorHandling.ConstexprCallThatAlwaysThrowsIsCompileError": "tipos de erro em `throws`",
    "ErrorHandling.SwitchArmsTypeMismatchDiagnostic": "tipos de erro em `throws`",
    "ErrorHandling.SwitchBlockArmReturnDiagnostic": "`switch` com bloco e `yield`",
    "ErrorHandling.SwitchBlockArmWithoutDivergenceDiagnostic": "`switch` com bloco e `yield`",
    "ErrorHandling.YieldWithValueOutsideSwitchArmDiagnostic": "`switch` com bloco e `yield`",
    "GenericConstraints.EnumConstraintViolated": "enum genérico com restrição",
    "PointerCompat.IncompatibleTypes": "conversão entre ponteiros incompatíveis",
    "Nullable.CastNullableToNonNullableEmitsWarning": "tipos anuláveis",
}
# The gtest sources predate the rule that only a wider type of the same kind converts implicitly: the port spells the cast.
EXPLICIT_CASTS = [
    (r"Math[.]", "return res;", "return (i32)res;"),
    (r"Math[.]Exp$", "return floor(e_squared);", "return (i32)floor(e_squared);"),
    (r"Math[.]Log$", "return round(ln_e);", "return (i32)round(ln_e);"),
    (r"Math[.]ImportMathMultipleFunctions", "return a + b + c;", "return (i32)(a + b + c);"),
    (r"Math[.]CombinedMathOperations", "return h;", "return (i32)h;"),
    (r"Hash[.]", "return x.hash();", "return (i32)x.hash();"),
    (r"Hash[.]", "i32 h = x.hash();", "i32 h = (i32)x.hash();"),
    (r"(Str|String)[.]", "return s.length;", "return (i32)s.length;"),
    (r"String[.]", "return s.capacity;", "return (i32)s.capacity;"),
    (r"String[.]", ".data = 0,", ".data = (i8*)0,"),
    (r"Arr[.]", "return nums.length;", "return (i32)nums.length;"),
    (r"NativeTypes[.]NintNegation", "return 0 - y;", "return (i32)(0 - y);"),
    (r"NativeTypes[.]NintAsFunctionSignature", "return r;", "return (i32)r;"),
]
GAP_BY_TOKEN = [
    ("require", "contratos (`require`/`ensure`)"),
    ("ensure", "contratos (`require`/`ensure`)"),
    ("yield", "`switch` com bloco e `yield`"),
    ("code", "`switch` com bloco e `yield`"),
    ("..", "`for` com range"),
    ("auto", "desestruturação"),
    ("sizeof", "`sizeof`/`typeof`/`alignof` de expressão"),
    ("typeof", "`sizeof`/`typeof`/`alignof` de expressão"),
    ("alignof", "`sizeof`/`typeof`/`alignof` de expressão"),
    ("expect", "intrínseca `expect`"),
    ("const", "`const` local"),
    ("!.", "tipos anuláveis"),
    ("?.", "tipos anuláveis"),
    ("??=", "tipos anuláveis"),
]
GAP_BY_SUITE = {
    "Contracts": "contratos (`require`/`ensure`)",
    "NonZero": "tipos non-zero (`i32n`)",
    "Nullable": "tipos anuláveis",
    "NativeTypes": "tipos nativos (`nint`)",
    "ErrorHandling": "tipos de erro em `throws`",
    "RuntimeErrors": "relatório de erro em tempo de execução",
    "Overflow": "modos de overflow",
    "Object": "`object` e `...args` com `str`",
    "IsExpression": "`object` e `...args` com `str`",
}
def extract(path):
    text = open(path, errors="replace", encoding="utf-8").read()
    marks = list(re.finditer(r"^[ ]*TEST(?:_F|_P)?[(]([A-Za-z0-9_]+),[ ]*([A-Za-z0-9_]+)[)]", text, re.M))
    for index, mark in enumerate(marks):
        end = marks[index + 1].start() if index + 1 < len(marks) else len(text)
        yield path.replace(os.sep, "/"), mark.group(1), mark.group(2), text[mark.end():end]


def with_explicit_casts(identifier, source):
    for pattern, implicit, explicit in EXPLICIT_CASTS:
        if re.match(pattern, identifier):
            source = source.replace(implicit, explicit)
    return source


def describe(file, suite, name, body):
    source = re.search(r'R"([a-zA-Z]*)[(](.*?)[)]\1"', body, re.S)
    helper = re.search(r"=[ ]*(run[A-Za-z]*|compile[A-Za-z]*|execute[A-Za-z]*)[(]", body)
    exit_code = re.search(r"EXPECT_EQ[(]result[.]returnCode,[ ]*(?:DJINN_EXIT[(])?(-?(?:0x[0-9a-fA-F]+|[0-9]+))[)]*;", body)
    diagnostics = re.search(r"EXPECT_EQ[(]result[.]diagnostics[.]size[(][)],[ ]*([0-9]+)", body)
    return {
        "file": file,
        "area": file.split("/")[1].replace(".cpp", ""),
        "id": suite + "." + name,
        "suite": suite,
        "source": with_explicit_casts(suite + "." + name, textwrap.dedent(source.group(2)).strip() + "\n") if source else None,
        "runs": "DjinnCompiler::run" in body or helper is not None,
        "binary": "generateBinary = true" in body or (helper is not None and helper.group(1).startswith(("run", "execute"))),
        "std": "includeStd = false" not in body,
        "exit": int(exit_code.group(1), 0) if exit_code else None,
        "exit_nonzero": re.search(r"EXPECT_NE[(]result[.]returnCode", body) is not None,
        "diagnostics": int(diagnostics.group(1)) if diagnostics else 0,
        "wants_diagnostic": re.search(r"EXPECT_G[TE][(]result[.]diagnostics[.]size|EXPECT_TRUE[(]hasError|errorCount[(]result[)],[ ]*[1-9]", body) is not None,
        "checks_ir": "result.ir" in body or "verifiedIr" in body,
        "checks_ast": "result.program" in body or "structDecl" in body,
        "checks_report": "runtimeErrorReport" in body,
    }


def call(command):
    try:
        done = subprocess.run(command, capture_output=True, timeout=60, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return None, ""
    code = done.returncode - 2 ** 32 if done.returncode >= 2 ** 31 else done.returncode
    return code, (done.stdout + done.stderr).decode("utf-8", "replace")


def execute(case, options, scratch):
    if not case["source"] or not case["runs"]:
        return case
    path = os.path.join(scratch, case["id"] + ".djinn")
    with open(path, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(case["source"])
    std = ["--std", "--std-dir", options.std_dir] if case["std"] else []
    code, output = call([options.compiler, path, "--check", "--diagnostics-format=json", "--runtime-dir", options.runtime_dir] + std)
    case["check"] = code
    case["warnings"] = re.findall(r'"severity":"warning","code":"(E[0-9]+)"', output)
    first = re.search(r'"severity":"error","code":"E([0-9]+)","file":"[^"]*","line":([0-9]+),"column":([0-9]+),"length":([0-9]+),"end_line":[0-9]+,"end_column":[0-9]+,"message":"((?:[^"]|[\\]")*)"', output)
    if first:
        lines = case["source"].split("\n")
        line, column, length = int(first.group(2)) - 1, int(first.group(3)) - 1, int(first.group(4))
        case["code"] = int(first.group(1))
        case["message"] = first.group(5)
        case["token"] = lines[line][column:column + max(length, 1)] if line < len(lines) else ""
    case["has_main"] = re.search(r"[ ]main[ ]*[(]", case["source"]) is not None
    if not first and code == 0 and case["binary"] and case["has_main"]:
        case["ran"], _ = call([options.compiler, path, "--run", "--runtime-lib", options.runtime_lib, "--runtime-dir", options.runtime_dir] + std)
    return case


def gap_of(case):
    if case["id"] in WRONG_REASON:
        return WRONG_REASON[case["id"]]
    for token, gap in GAP_BY_TOKEN:
        if case.get("token", "").startswith(token):
            return gap
    if case["suite"] in GAP_BY_SUITE:
        return GAP_BY_SUITE[case["suite"]]
    if "message" in case:
        return "o djinnc rejeita: " + case["message"]
    return "o djinnc não reproduz o comportamento"


def decide(case):
    file = case["file"]
    if case["id"] in SUPERSEDED:
        return "Descartar", SUPERSEDED[case["id"]]
    if file in INTERNAL_FILES:
        return "Descartar", INTERNAL_FILES[file]
    if file in REWRITE_FILES and not (case["source"] and case["runs"]):
        return "Reescrever", REWRITE_FILES[file]
    if not case["source"] or not case["runs"]:
        return "Bloqueado", gap_of(case)
    if case["id"] in WRONG_REASON:
        return "Bloqueado", gap_of(case) + " (o erro atual tem outro motivo)"
    if case["checks_report"]:
        return "Reescrever", "confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico"
    wants_error = case["diagnostics"] > 0 or case["wants_diagnostic"]
    if wants_error:
        if "code" in case:
            return "Portar", "erro E%d" % case["code"]
        if case["warnings"]:
            case["warning"] = int(case["warnings"][0][1:])
            return "Portar", "aviso " + case["warnings"][0]
        return "Bloqueado", gap_of(case)
    if "code" in case:
        return "Bloqueado", gap_of(case)
    if "ran" in case:
        if case["exit"] is not None:
            if case["ran"] == case["exit"]:
                return "Portar", "exit code %d" % case["exit"]
            return "Bloqueado", gap_of(case) + " (esperado %d, veio %s)" % (case["exit"], case["ran"])
        if case["exit_nonzero"]:
            if case["ran"] != 0:
                return "Portar", "exit code diferente de zero"
            return "Bloqueado", gap_of(case)
        return "Portar", "só precisa rodar"
    if case["checks_ir"]:
        return "Reescrever", "confere IR; depende de `ir_contains`"
    if case["checks_ast"]:
        return "Portar", "compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc"
    return "Portar", "compila sem erros"


def shape(source):
    return " ".join(re.sub(r"//[^\n]*", "", source).split())


def written_tests():
    owners = (("samples", "SamplesTests"), ("errors", "ErrorsTests"), ("floats", "FloatIeeeTests"), ("std", "StdTests"),
              ("crash", "CrashTests"), ("debug", "DebugTests"), ("library", "LibraryTests"))
    for area, owner in owners:
        text = open(os.path.join(SUITES, area + ".djinn"), encoding="utf-8").read()
        for name, source in re.findall(r'void (\w+)[(][)] [{]\n +str source = """\n(.*?)\n""";', text, re.S):
            yield source, "%s.%s" % (owner, name)


def mark_duplicates(cases):
    seen = {}
    for source, name in written_tests():
        seen.setdefault(shape(source), name)
    for case in cases:
        if case["decision"] != "Portar" or not case["source"]:
            continue
        key = shape(case["source"]) + "|" + case["reason"]
        twin = seen.get(key) or seen.get(shape(case["source"]))
        if twin:
            case["decision"], case["reason"] = "Descartar", "duplicado de `%s`" % twin
        else:
            seen[key] = case["id"]


def diagnostic_names():
    text = open("djinnc/src/support/source/diagnostic_code.djinn", encoding="utf-8").read()
    names = {}
    for group, body in re.findall(r"struct (Diagnostic(?:Errors|Warnings)) [{](.*?)[}]", text, re.S):
        for name, code in re.findall(r"const i32 ([A-Z_0-9]+) = ([0-9]+);", body):
            names[int(code)] = group + "." + name
    return names


def literal(text):
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")


def write_suite(area, groups):
    os.makedirs(SUITES, exist_ok=True)
    structs = "\n".join("struct %sTests {\n%s}\n" % (group, "\n".join(methods)) for group, methods in groups.items())
    with open(os.path.join(SUITES, area + ".djinn"), "w", encoding="utf-8", newline="\n") as stream:
        stream.write(HEADER + "\n" + structs)


def check(case, names):
    std = ".with_std()" if case["std"] else ""
    if case["reason"].startswith("erro"):
        return "%s.diagnostics(Expected.error(%s))" % (std, names.get(case["code"], case["code"]))
    if case["reason"].startswith("aviso"):
        return "%s.diagnostics(Expected.warning(%s))" % (std, names.get(case["warning"], case["warning"]))
    if "ran" in case:
        expected = "Expected.failure()" if case["exit"] is None and case["exit_nonzero"] else "Expected.exit(%d)" % (case["exit"] or 0)
        return "%s.runs(%s)" % (std, expected)
    return "%s.compiles()" % std


def test_method(case, names):
    name = case["id"].split(".", 1)[1]
    if name[0].isdigit():
        name = "test_" + name
    source = case["source"].rstrip("\n")
    assert '"""' not in source, case["id"]
    return '    [Test]\n    void %s() {\n        str source = """\n%s\n""";\n        Verify.source(source)%s;\n    }\n' % (name, source, check(case, names))


def write_tests(cases):
    names = diagnostic_names()
    areas = collections.defaultdict(lambda: collections.defaultdict(list))
    for case in cases:
        if case["decision"] == "Portar":
            areas[case["area"]][case["suite"]].append(test_method(case, names))
    for area, groups in areas.items():
        write_suite(area, groups)


def write_report(cases, path):
    totals = collections.Counter(case["decision"] for case in cases)
    gaps = collections.Counter(re.sub(r" [(].*$", "", case["reason"]) for case in cases if case["decision"] == "Bloqueado")
    lines = ["# Triagem dos testes gtest", "",
             "Gerado por `tests/conformance/port_gtests.py`: cada `TEST` foi extraído e executado no djinnc, e a decisão sai do",
             "resultado. Os grupos que falharam foram revisados à mão; os que passaram não tiveram a intenção lida um a um.", "",
             "| Decisão | Testes | O que significa |", "|---|---|---|",
             "| Portar | %d | Passa no djinnc; está em `tests/conformance/src/suites`, como `[Test]`. |" % totals["Portar"],
             "| Reescrever | %d | O comportamento importa, mas o teste precisa de outra forma. |" % totals["Reescrever"],
             "| Bloqueado | %d | O djinnc não reproduz o comportamento; falta decidir se implementa ou se o recurso sai. |" % totals["Bloqueado"],
             "| Descartar | %d | Interno do stage0 ou duplicado. |" % totals["Descartar"], "",
             "## O que bloqueia", "", "| Lacuna | Testes |", "|---|---|"]
    lines += ["| %s | %d |" % (gap.replace("|", "\\|"), count) for gap, count in gaps.most_common()]
    for area in sorted({case["area"] for case in cases}):
        lines += ["", "## " + area, "", "| Teste | Decisão | Motivo |", "|---|---|---|"]
        lines += ["| %s | %s | %s |" % (case["id"], case["decision"], case["reason"].replace("|", "\\|")) for case in cases if case["area"] == area]
    with open(path, "w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines) + "\n")
    print(dict(totals))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler")
    parser.add_argument("runtime_lib")
    parser.add_argument("--runtime-dir", default="runtime")
    parser.add_argument("--std-dir", default="std")
    parser.add_argument("--jobs", type=int, default=8)
    options = parser.parse_args()
    paths = [path for path in sorted(glob.glob("tests/**/*.cpp", recursive=True)) if "djinnc" not in path and "conformance" not in path]
    cases = [describe(*found) for path in paths for found in extract(path)]
    with tempfile.TemporaryDirectory() as scratch, concurrent.futures.ThreadPoolExecutor(options.jobs) as pool:
        cases = list(pool.map(lambda case: execute(case, options, scratch), cases))
    for case in cases:
        case["decision"], case["reason"] = decide(case)
    mark_duplicates(cases)
    write_tests(cases)
    write_report(cases, "tests/conformance/TRIAGE.md")


if __name__ == "__main__":
    main()
