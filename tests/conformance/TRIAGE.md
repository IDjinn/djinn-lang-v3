# Triagem dos testes gtest

Gerado por `tests/conformance/port_gtests.py`: cada `TEST` foi extraído e executado no djinnc, e a decisão sai do
resultado. Os grupos que falharam foram revisados à mão; os que passaram não tiveram a intenção lida um a um.

| Decisão | Testes | O que significa |
|---|---|---|
| Portar | 439 | Passa no djinnc; está em `tests/conformance/src/suites`, como `[Test]`. |
| Reescrever | 87 | O comportamento importa, mas o teste precisa de outra forma. |
| Bloqueado | 154 | O djinnc não reproduz o comportamento; falta decidir se implementa ou se o recurso sai. |
| Descartar | 43 | Interno do stage0 ou duplicado. |

## O que bloqueia

| Lacuna | Testes |
|---|---|
| tipos de erro em `throws` | 30 |
| modos de overflow | 23 |
| contratos | 20 |
| `sizeof`/`typeof`/`alignof` de expressão | 18 |
| `switch` com bloco e `yield` | 10 |
| tipos non-zero | 8 |
| tipos anuláveis | 7 |
| `object` e `...args` com `str` | 6 |
| desestruturação | 4 |
| `for` com range | 4 |
| o djinnc rejeita: undefined variable | 3 |
| o djinnc não reproduz o comportamento | 3 |
| tipos nativos | 3 |
| `const` local | 2 |
| o djinnc rejeita: struct does not define this operator | 2 |
| o djinnc rejeita: an enum cannot carry a payload: declare it as a union | 2 |
| o djinnc rejeita: struct does not define operator get[] | 1 |
| o djinnc rejeita: a struct initializer needs a struct type from its context | 1 |
| o djinnc rejeita: expected a type | 1 |
| o djinnc rejeita: expected '(' after function name | 1 |
| intrínseca `expect` | 1 |
| enum genérico com restrição | 1 |
| o djinnc rejeita: expected '{' | 1 |
| o djinnc rejeita: struct has no such field | 1 |
| conversão entre ponteiros incompatíveis | 1 |

## aggregates

| Teste | Decisão | Motivo |
|---|---|---|
| Destructuring.BasicStructFields | Bloqueado | desestruturação |
| Destructuring.VariablesVisibleInEnclosingScope | Bloqueado | desestruturação |
| Destructuring.SingleField | Bloqueado | desestruturação |
| Destructuring.FromExpression | Bloqueado | desestruturação |
| Destructuring.BareBlockStillIntroducesScope | Portar | erro E3002 |
| Enum.SimpleDefinition | Portar | exit code 0 |
| Enum.VariantConstruction | Portar | exit code 0 |
| Enum.VariantWithPayload | Portar | exit code 0 |
| Enum.MultiplePayloadTypes | Portar | exit code 0 |
| Enum.VariantWithMultipleFields | Portar | exit code 0 |
| Enum.GenericOptional | Portar | exit code 0 |
| Enum.GenericResult | Portar | exit code 0 |
| Enum.GenericWithPointerType | Portar | exit code 0 |
| Enum.GenericWithFloatType | Portar | exit code 0 |
| Enum.GenericMultipleInstantiations | Portar | exit code 0 |
| Enum.PatternMatchBasic | Portar | exit code 69 |
| Enum.PatternMatchEmpty | Portar | exit code -1 |
| Enum.PatternMatchResult | Portar | exit code 42 |
| Enum.PatternMatchResultError | Portar | exit code 7 |
| Enum.PatternMatchWithComputation | Portar | exit code 21 |
| Enum.PatternMatchNoPayload | Portar | exit code 2 |
| Enum.PatternMatchInFunction | Portar | exit code 149 |
| Enum.PatternMatchWildcardArm | Portar | exit code -1 |
| Hash.I32HashIsIdentity | Portar | exit code 42 |
| Hash.U8HashIsIdentity | Portar | exit code 20 |
| Hash.I16HashIsIdentity | Portar | exit code 99 |
| Hash.U16HashIsIdentity | Portar | exit code 150 |
| Hash.I8HashIsIdentity | Portar | exit code 77 |
| Hash.U32HashIsIdentity | Portar | exit code 55 |
| Hash.I64HashProducesNonZero | Portar | exit code diferente de zero |
| Hash.U64HashProducesNonZero | Portar | exit code diferente de zero |
| Hash.128HashProducesNonZero | Portar | exit code diferente de zero |
| Hash.U128HashProducesNonZero | Portar | exit code diferente de zero |
| Hash.HexLiteralHash | Portar | exit code 255 |
| Hash.BinaryLiteralHash | Portar | exit code 42 |
| IndexAccess.BasicRead | Portar | exit code 20 |
| IndexAccess.Write | Portar | exit code 42 |
| IndexAccess.WithMalloc | Portar | exit code 30 |
| IndexAccess.I8Pointer | Portar | exit code 65 |
| Str.LiteralCreatesStrStruct | Portar | exit code 5 |
| Str.EmptyLiteral | Portar | exit code 0 |
| Str.CoercionToI8PtrForPrintf | Portar | exit code 0 |
| Str.DirectLiteralCoercion | Portar | exit code 0 |
| Str.LenField | Portar | exit code 10 |
| Str.IsEmptyMethodTrue | Portar | exit code 1 |
| Str.IsEmptyMethodFalse | Portar | exit code 0 |
| Str.IndexAccess | Bloqueado | o djinnc rejeita: struct does not define operator get[] |
| Str.PassAsParameter | Portar | exit code 4 |
| Arr.LiteralCreatesArrStruct | Portar | exit code 3 |
| Arr.IndexRead | Portar | exit code 20 |
| Arr.IndexWrite | Portar | exit code 99 |
| Arr.LenField | Portar | exit code 5 |
| Arr.IsEmptyMethodFalse | Portar | exit code 0 |
| Arr.SumElements | Portar | exit code 100 |
| Arr.WithPrintf | Portar | exit code 15 |
| FixedArray.BasicAllocation | Portar | exit code 42 |
| FixedArray.WriteAndReadMultiple | Portar | exit code 60 |
| FixedArray.WithConstSize | Bloqueado | `const` local |
| FixedArray.LoopFill | Portar | exit code 30 |
| FixedArray.LargeBuffer | Portar | exit code 99 |
| String.StructDefinition | Portar | exit code 0 |
| String.IsEmptyMethod | Portar | exit code 1 |
| String.CapacityField | Portar | exit code 16 |
| Struct.Definition | Portar | exit code 0 |
| Struct.BraceInitializerPositional | Portar | exit code 0 |
| Struct.BraceInitializerDesignated | Portar | exit code 0 |
| Struct.FieldAccess | Portar | exit code 42 |
| Struct.FieldAccessSecondField | Portar | exit code 99 |
| Struct.MethodReturnWithBraceInit | Portar | exit code 30 |
| Struct.AutoInferStructType | Portar | exit code 77 |
| Struct.MultipleFieldsPositional | Portar | exit code 6 |
| Struct.FieldAccessInExpression | Portar | exit code 5 |
| Struct.StructAsParameter | Portar | exit code 123 |
| Struct.SimpleBraceInitScalar | Bloqueado | o djinnc rejeita: a struct initializer needs a struct type from its context |
| Impl.ConstFieldOnPrimitive | Bloqueado | o djinnc rejeita: undefined variable |
| Impl.ConstFieldWithArithmetic | Bloqueado | o djinnc rejeita: undefined variable |
| Impl.MethodOnStruct | Portar | exit code 42 |
| Impl.ConstAndMethodTogether | Portar | exit code 30 |
| Impl.StaticMethodOnPrimitive | Bloqueado | o djinnc rejeita: undefined variable |

## arithmetic

| Teste | Decisão | Motivo |
|---|---|---|
| Math.IntegerDivFunction | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Math.IntegerDiv | Portar | exit code 5 |
| Math.IntegerDivWithRemainder | Portar | exit code 3 |
| Math.Sqrt | Portar | exit code 4 |
| Math.SqrtFloat | Portar | exit code 5 |
| Math.Pow | Portar | exit code 8 |
| Math.Floor | Portar | exit code 3 |
| Math.Ceil | Portar | exit code 4 |
| Math.Round | Portar | exit code 4 |
| Math.Fabs | Portar | exit code 42 |
| Math.Fmax | Portar | exit code 20 |
| Math.Fmin | Portar | exit code 5 |
| Math.Cbrt | Portar | exit code 3 |
| Math.Exp | Portar | exit code 7 |
| Math.Log | Portar | exit code 1 |
| Math.Log10 | Portar | exit code 3 |
| Math.Log2 | Portar | exit code 3 |
| Math.Fmod | Portar | exit code 1 |
| Math.Hypot | Portar | exit code 5 |
| Math.Trunc | Portar | exit code 7 |
| Math.Exp2 | Portar | exit code 16 |
| Math.ImportMathSqrt | Portar | exit code 7 |
| Math.ImportMathPow | Portar | exit code 9 |
| Math.ImportMathMultipleFunctions | Portar | exit code 10 |
| Math.ImportMathFloatFunctions | Portar | exit code 6 |
| Math.CombinedMathOperations | Portar | exit code 5 |
| Math.IntegerMultFunction | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Math.IntegerMult | Portar | exit code 6 |
| Numbers.MultipleNumbersDeclaration | Portar | compila sem erros |
| Numbers.CompileTimeOverflowCheck | Descartar | aceitava literais fora da faixa, que agora são erro; coberto por `errors/literal_out_of_range` |
| Overflow.SuffixDeclarationsCompile | Portar | compila sem erros |
| Overflow.WrappedIsPlainAdd | Reescrever | confere IR; depende de `ir_contains` |
| Overflow.TrappedEmitsOverflowCheck | Reescrever | confere IR; depende de `ir_contains` |
| Overflow.TrappedRuntimeOverflowAborts | Bloqueado | modos de overflow (o erro atual tem outro motivo) |
| Overflow.CheckedRequiresThrows | Bloqueado | modos de overflow |
| Overflow.CheckedWithThrowsSetsErrorFlag | Reescrever | confere IR; depende de `ir_contains` |
| Overflow.SaturatingUsesSatIntrinsic | Reescrever | confere IR; depende de `ir_contains` |
| Overflow.SaturatingRuntimeAddClampsToMax | Bloqueado | modos de overflow (esperado 2, veio 0) |
| Overflow.SaturatingLiteralClampsAtCompileTime | Bloqueado | modos de overflow |
| Overflow.TrappedLiteralOutOfRangeIsCompileError | Portar | erro E3051 |
| Overflow.WrappedLiteralOutOfRangeStillCompiles | Bloqueado | modos de overflow |
| Overflow.ConflictingModesAreRejected | Bloqueado | modos de overflow |
| Overflow.PostfixIncrementRespectsMode | Portar | exit code 2 |
| Overflow.FloatTypeSuffixIsRejected | Bloqueado | modos de overflow (o erro atual tem outro motivo) |
| Overflow.WrappedLiteralKeepsCStyleValue | Bloqueado | modos de overflow |
| Overflow.HexLiteralSuffixCompiles | Bloqueado | modos de overflow |
| Overflow.HexDigitCIsNotASuffix | Portar | exit code 28 |
| Overflow.BinaryLiteralSuffix | Portar | exit code 10 |
| Overflow.CompoundAssignmentSaturates | Bloqueado | modos de overflow (esperado 2, veio -1) |
| Overflow.CompoundAssignmentTrappedAborts | Bloqueado | modos de overflow (o erro atual tem outro motivo) |
| Overflow.UnaryNegateSaturatesMinToMax | Bloqueado | modos de overflow (esperado 2, veio -2) |
| Overflow.UnaryNegateTrappedAborts | Bloqueado | modos de overflow (o erro atual tem outro motivo) |
| Overflow.DivIntMinMinusOneTrappedAborts | Portar | exit code diferente de zero |
| Overflow.SaturatingDivIntMinClampsToMax | Bloqueado | modos de overflow (esperado 2, veio -1073740791) |
| Overflow.SaturatingRemIntMinIsZero | Portar | exit code 5 |
| Overflow.SaturatingSubClampsToMin | Bloqueado | modos de overflow (esperado 2, veio 0) |
| Overflow.SaturatingMulClampsToMax | Bloqueado | modos de overflow (esperado 2, veio 0) |
| Overflow.UnsignedSaturatingSubClampsToZero | Bloqueado | modos de overflow |
| Overflow.UnsignedTrappedAddAborts | Bloqueado | modos de overflow |
| Overflow.ConstexprSaturatingAdd | Bloqueado | modos de overflow (esperado 2, veio 0) |
| Overflow.ConstevalTrappedOverflowIsError | Bloqueado | modos de overflow |
| Overflow.NestedModePropagationSaturates | Bloqueado | modos de overflow (esperado 2, veio -1) |
| Overflow.CheckedOverflowCaughtByTryFallback | Bloqueado | modos de overflow (esperado 1, veio -2147483648) |
| Overflow.CheckedNoOverflowReturnsValue | Portar | exit code 1032 |
| Overflow.ModeIsNotTypeIdentity | Portar | exit code 100 |
| Overflow.StructFieldWithMode | Portar | exit code 42 |
| Math.IntegerSubFunction | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Math.IntegerSub | Portar | exit code 7 |
| Math.IntegerSubNegative | Portar | exit code -7 |
| Math.IntegerSumFunction | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Math.IntegerSum | Portar | exit code 5 |
| Math.IntegerSumWithAnonStructReturn | Bloqueado | o djinnc rejeita: expected a type |

## async

| Teste | Decisão | Motivo |
|---|---|---|
| Async.SimpleReturnValue | Portar | exit code 42 |
| Async.WithParameter | Portar | exit code 42 |
| Async.MultipleParameters | Portar | exit code 42 |
| Async.ExpressionComputation | Portar | exit code 21 |
| Async.AsyncMain | Portar | exit code 99 |
| Async.AsyncMainWithComputation | Portar | exit code 25 |
| Async.DiagnosticsForNonWaitableAsync | Portar | erro E3053 |
| Async.MultipleAwaitsSequential | Portar | exit code 30 |
| Async.ThreeAwaitsAccumulate | Portar | exit code 60 |
| Async.ChainedAwait | Portar | exit code 15 |
| Async.DeepChaining | Portar | exit code 13 |
| Async.ChainedAsyncMain | Portar | exit code 35 |
| Async.AwaitResultAsArgument | Portar | exit code 20 |
| Async.AwaitWithLocalComputation | Portar | exit code 33 |
| Async.Yield_BasicYield | Bloqueado | `switch` com bloco e `yield` |
| Async.Yield_MultipleYields | Bloqueado | `switch` com bloco e `yield` |
| Async.Yield_WithComputation | Bloqueado | `switch` com bloco e `yield` |
| Spawn.BasicSpawnParse | Reescrever | confere IR; depende de `ir_contains` |
| EventLoop.AsyncMainGeneratesRuntimeCalls | Reescrever | confere IR; depende de `ir_contains` |
| EventLoop.CoroWrappersGenerated | Reescrever | confere IR; depende de `ir_contains` |
| Async.NonAsyncMainStillWorks | Portar | exit code 77 |
| Async.NonAsyncMainWithRuntime | Reescrever | confere IR; depende de `ir_contains` |
| Spawn.MultipleSpawnsParse | Portar | compila sem erros |
| Spawn.WithYieldParse | Bloqueado | `switch` com bloco e `yield` |

## compile_time

| Teste | Decisão | Motivo |
|---|---|---|
| Attributes.ForceInlineGeneratesAlwaysInline | Reescrever | confere IR; depende de `ir_contains` |
| Attributes.NoInlineGeneratesNoInline | Reescrever | confere IR; depende de `ir_contains` |
| Attributes.NoReturnAttribute | Reescrever | confere IR; depende de `ir_contains` |
| Attributes.ColdAttribute | Reescrever | confere IR; depende de `ir_contains` |
| Attributes.ParameterizedAttributeParses | Portar | compila sem erros |
| Attributes.NamedParameterAttributeParses | Portar | compila sem erros |
| Attributes.MultipleAttributesOnMethod | Reescrever | confere IR; depende de `ir_contains` |
| Attributes.ImplicitNounwindOnAllFunctions | Reescrever | confere IR; depende de `ir_contains` |
| Attributes.LlvmEscapeHatch | Reescrever | confere IR; depende de `ir_contains` |
| Attributes.ErrorOnUnknownAttribute | Portar | erro E8002 |
| Attributes.ErrorOnUnknownStructAttribute | Portar | erro E8002 |
| Attributes.WarningWhenFunctionAttributeOnStruct | Portar | aviso E8001 |
| Attributes.WarningWhenStructAttributeOnMethod | Bloqueado | o djinnc não reproduz o comportamento |
| Attributes.NoWarningWhenAttributeOnValidTarget | Portar | compila sem erros |
| Attributes.DeprecatedValidOnAnyTarget | Portar | compila sem erros |
| ConstEvalVM.IntegerLiteral | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.FloatLiteral | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.BooleanLiteral | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.BooleanFalseLiteral | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.HexLiteral | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.BinaryLiteral | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.SeparatorLiteral | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.TickSeparatorLiteral | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.NegativeZero | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.Addition | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.Subtraction | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.Multiplication | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.Division | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.Modulo | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.DivisionByZero | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.ModuloByZero | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.UnaryNegate | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.DoubleNegate | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.NestedArithmetic | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.ComplexNestedArithmetic | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.LessThan | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.LessThanFalse | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.GreaterThan | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.LessEqual | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.GreaterEqual | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.Equality | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.Inequality | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.LogicAnd | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.LogicOr | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.LogicalNot | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.LogicalNotFalse | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.NamedConstant | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.NamedConstantInExpression | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.MultipleNamedConstants | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.UnknownIdentifierReturnsError | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.MaxIterationsExceeded | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstEvalVM.StackOverflowProtection | Reescrever | usa a API C++ do avaliador de constantes; vira um caso constexpr em Djinn |
| ConstExpr.SimpleConstExprVariable | Portar | exit code 42 |
| ConstExpr.ConstExprDependingOnLaterDeclaration | Portar | exit code 41 |
| ConstExpr.ConstExprWithArithmetic | Portar | exit code 35 |
| ConstExpr.ConstExprMultiplication | Portar | exit code 192 |
| ConstExpr.ConstExprNegative | Portar | exit code 42 |
| ConstExpr.ConstExprUsedInArraySize | Portar | exit code 99 |
| ConstExpr.ConstFieldUsedInExpression | Portar | exit code 99 |
| ConstExpr.ConstFieldWithArithmetic | Portar | exit code 192 |
| ConstExpr.ConstExprFunctionCompiles | Portar | compila sem erros |
| ConstExpr.ConstExprFunctionResult | Portar | exit code 42 |
| ConstExpr.LocalConstExprVariable | Portar | exit code 42 |
| ConstExpr.LocalConstevalVariable | Portar | exit code 42 |
| ConstExpr.LocalConstExprUsedInExpression | Portar | exit code 30 |
| ConstExpr.LocalConstExprInArraySize | Portar | exit code 77 |
| ConstExpr.GlobalConstevalVariable | Portar | exit code 7 |
| ConstExpr.InvalidConstExprAndAsyncModifier | Portar | erro E2001 |
| CompileTimeIf.TrueBranch | Portar | exit code 42 |
| CompileTimeIf.FalseBranch | Portar | exit code 42 |
| CompileTimeIf.FalseBranchWithElse | Portar | exit code 42 |
| CompileTimeIf.TrueBranchWithElse | Portar | exit code 42 |
| CompileTimeIf.BinaryExpression | Portar | exit code 42 |
| CompileTimeIf.CompoundExpression | Portar | exit code 42 |
| CompileTimeIf.ElseIfChain | Portar | exit code 42 |
| CompileTimeIf.WarningOnConstExprInRuntimeIf | Portar | exit code 42 |
| CompileTimeIf.NoWarningOnRuntimeVariable | Portar | exit code 42 |
| CompileTimeIf.ErrorOnNonConstExprWithoutParens | Portar | erro E3051 |
| CompileTimeIf.LiteralCondition | Portar | exit code 42 |
| CompileTimeIf.DeadCodeEliminated | Portar | exit code 10 |
| CompileTimeIf.ExplicitConstexprKeyword | Portar | exit code 42 |
| CompileTimeIf.ExplicitConstevalKeyword | Portar | exit code 42 |
| CompileTimeIf.ConstevalErrorOnRuntimeExpr | Portar | erro E3051 |
| CompileTimeIf.ExplicitConstexprWithElseIf | Portar | exit code 42 |
| CompileTimeIf.NoWarningOnMixedExpr | Portar | exit code 42 |
| Intrinsic.IntrinsicConstevalVariable | Bloqueado | o djinnc rejeita: expected '(' after function name |
| Intrinsic.ConstexprStructFieldAccess | Portar | exit code 42 |
| Intrinsic.ConstexprStructAllFieldsAreConst | Portar | exit code 99 |
| Contracts.RequirePasses | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.RequireViolationThrowsContractViolation | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.EnsurePasses | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.EnsureViolationThrowsContractViolation | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.MultipleClauses | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.RequireBlockForm | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.ContractCallSiteRequiresTry | Bloqueado | contratos (`require`/`ensure`) (o erro atual tem outro motivo) |
| Contracts.ContractViolationPropagatesThroughThrows | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.RequireConstantViolationIsCompileError | Bloqueado | contratos (`require`/`ensure`) (o erro atual tem outro motivo) |
| Contracts.RuntimeArgumentsSkipCompileTimeAnalysis | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.EnforcementRuntimeKeepsOldBehavior | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.EnforcementStrictRequiresTryEverywhere | Bloqueado | contratos (`require`/`ensure`) (o erro atual tem outro motivo) |
| Contracts.EnforcementStrictAcceptsBareTryPropagation | Bloqueado | contratos (`require`/`ensure`) |
| Contracts.EnforcementOffDisablesAllChecks | Bloqueado | contratos (`require`/`ensure`) |
| Intrinsics.SizeofI32 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.SizeofI64 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.SizeofI8 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.SizeofStruct | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.AlignofI32 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.AlignofI64 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.Likely | Portar | exit code 42 |
| Intrinsics.Unlikely | Portar | exit code 99 |
| Intrinsics.Expect | Bloqueado | intrínseca `expect` |
| Intrinsics.SizeofF32 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.SizeofF64 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.SizeofGenericStructI32 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.SizeofGenericStructI64 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.SizeofGenericStructPoint | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.TypeofI32 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.TypeofF64 | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Intrinsics.TypeofStruct | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| Macro.SimpleExprMacro | Portar | aviso E6001 |
| Macro.MultipleParameters | Portar | exit code 42 |
| Macro.PrecedencePreserved | Portar | exit code 25 |
| Macro.LocalAvoidsDoubleEvaluation | Portar | exit code 1 |
| Macro.WithoutLocalCausesDoubleEvaluation | Portar | aviso E6001 |
| Macro.LocalWithArithmeticExpression | Portar | exit code 49 |
| Macro.MixedLocalAndNonLocal | Portar | exit code 19 |
| Macro.NestedMacroCall | Portar | exit code 25 |
| Macro.WarnOnPossibleSideEffect | Portar | exit code 25 |
| Macro.NoWarnWhenLocalUsed | Portar | exit code 25 |
| Macro.NoWarnWhenSingleUse | Portar | exit code 42 |
| Macro.MacroStoredInProgram | Portar | exit code 10 |
| Macro.MultiRuleByArity | Portar | exit code 25 |
| Macro.AmbiguousRulesError | Portar | erro E6000 |
| Macro.MultiRuleNoMatch | Portar | erro E6000 |
| Macro.LiteralTokenMatching | Portar | exit code 36 |
| Macro.LiteralTokenNoMatch | Portar | erro E6000 |
| Macro.IdentifierFragment | Portar | exit code 42 |
| Macro.LiteralTokenWithMultiRule | Portar | exit code 33 |
| Macro.RuleLevelLocal | Portar | exit code 5 |
| Macro.LogWithLiteralTokenLevels | Portar | exit code 30 |
| Macro.LogLiteralTokenNoMatchLevel | Portar | erro E6000 |
| Macro.LogMultiRuleWithIdentifier | Portar | exit code 3 |
| Macro.LiteralTokenWithIdentifierAndExpression | Portar | exit code 109 |
| Macro.LiteralTokenPatternMatchingComplex | Portar | exit code 78 |
| Macro.LogPatternLikeRust | Portar | exit code 15 |
| Macro.ConditionalCodegenViaLiteralToken | Portar | exit code 50 |
| Macro.LiteralFragment | Portar | exit code 42 |
| Macro.LiteralFragmentRejectsExpression | Portar | erro E6000 |
| Macro.TypeFragment | Portar | exit code 123 |
| Macro.LiteralTokenWithLiteralFragment | Portar | exit code 1130 |
| StaticVar.SimpleImmutable | Portar | exit code 42 |
| StaticVar.Mutable | Portar | exit code 3 |
| StaticVar.MultipleGlobals | Portar | exit code 42 |
| StaticVar.MutableWithFunctions | Portar | exit code 42 |
| StaticVar.UsedInExpressions | Portar | exit code 42 |
| StaticVar.MutableAccumulator | Portar | exit code 42 |
| StaticVar.MutableRequiresUnsafe | Portar | erro E2001 |
| StaticVar.UnsafeRequiresMutable | Portar | erro E2001 |

## compiler

| Teste | Decisão | Motivo |
|---|---|---|
| Binder.AllowsValidCode | Portar | exit code 30 |
| Binder.VariableDeclarationAndUsage | Portar | exit code 42 |
| Binder.FunctionCallWithCorrectArgs | Portar | exit code 6 |
| Binder.NamespaceQualifiedNames | Portar | compila sem erros |
| Binder.NestedNamespaceQualifiedNames | Portar | compila sem erros |
| Binder.FileScopedNamespaceBinding | Portar | compila sem erros |
| Binder.StructFieldAccess | Portar | exit code 15 |
| Binder.MultipleVariables | Portar | exit code 6 |
| BootstrapSubset.PointerParameterFieldReadWrite | Portar | exit code 42 |
| BootstrapSubset.PointerLocalMethodCall | Portar | exit code 2 |
| BootstrapSubset.PointerParameterInMethod | Portar | exit code 10 |
| BootstrapSubset.NullableLinkedListTraversal | Bloqueado | o djinnc rejeita: struct does not define this operator |
| BootstrapSubset.LogicalOperatorsShortCircuit | Bloqueado | o djinnc rejeita: struct does not define this operator |
| BootstrapSubset.PointerArrayOfStructPointers | Portar | exit code 6 |
| BootstrapSubset.StructTypesVisibleAcrossFiles | Reescrever | compila vários arquivos pela API C++; vira um caso multiarquivo |
| Diagnostics.BasicError | Descartar | testa a formatação do DiagnosticEngine do stage0 |
| Diagnostics.ErrorWithHelp | Descartar | testa a formatação do DiagnosticEngine do stage0 |
| Diagnostics.MultipleErrors | Descartar | testa a formatação do DiagnosticEngine do stage0 |
| Diagnostics.CodeFormatting | Descartar | testa a formatação do DiagnosticEngine do stage0 |
| FunctionContext.GenericMethodInsideLoopOfContractFunction | Bloqueado | contratos (`require`/`ensure`) |
| FunctionContext.BreakAndContinueAfterGenericInstantiation | Portar | exit code 9 |
| FunctionContext.MonomorphizedStructVisibleToLaterFunctions | Portar | exit code 7 |
| FunctionContext.PointerFieldMethodDispatchThroughAuto | Portar | exit code 9 |
| Determinism.SameSourceProducesIdenticalIr | Portar | compila sem erros |
| ManglerType.Integer8Signed | Descartar | testa o mangler do stage0 |
| ManglerType.Integer16Signed | Descartar | testa o mangler do stage0 |
| ManglerType.Integer32Signed | Descartar | testa o mangler do stage0 |
| ManglerType.Integer64Signed | Descartar | testa o mangler do stage0 |
| ManglerType.Integer32Unsigned | Descartar | testa o mangler do stage0 |
| ManglerType.Integer64Unsigned | Descartar | testa o mangler do stage0 |
| ManglerType.Void | Descartar | testa o mangler do stage0 |
| ManglerType.Float32 | Descartar | testa o mangler do stage0 |
| ManglerType.Float64 | Descartar | testa o mangler do stage0 |
| ManglerType.String | Descartar | testa o mangler do stage0 |
| ManglerType.Pointer | Descartar | testa o mangler do stage0 |
| ManglerType.PointerToPointer | Descartar | testa o mangler do stage0 |
| ManglerType.Array | Descartar | testa o mangler do stage0 |
| ManglerType.Struct | Descartar | testa o mangler do stage0 |
| ManglerType.StructShortName | Descartar | testa o mangler do stage0 |
| ManglerFunction.SimpleName | Descartar | testa o mangler do stage0 |
| ManglerFunction.LongerName | Descartar | testa o mangler do stage0 |
| ManglerFunction.SingleCharName | Descartar | testa o mangler do stage0 |
| ManglerFunction.WithIntParam | Descartar | testa o mangler do stage0 |
| ManglerFunction.WithMultipleParams | Descartar | testa o mangler do stage0 |
| ManglerFunction.WithStructParam | Descartar | testa o mangler do stage0 |
| ManglerFunction.WithPointerParam | Descartar | testa o mangler do stage0 |
| ManglerMethod.SimpleMethod | Descartar | testa o mangler do stage0 |
| ManglerMethod.MethodWithParams | Descartar | testa o mangler do stage0 |
| ManglerMethod.MethodWithMultipleParams | Descartar | testa o mangler do stage0 |
| ManglerMethod.GetterMethod | Descartar | testa o mangler do stage0 |
| ManglerGeneric.StructSingleTypeArg | Descartar | testa o mangler do stage0 |
| ManglerGeneric.StructMultipleTypeArgs | Descartar | testa o mangler do stage0 |
| ManglerGeneric.StructWithStructTypeArg | Descartar | testa o mangler do stage0 |
| ManglerGeneric.NestedGeneric | Descartar | testa o mangler do stage0 |
| ManglerGenericMethod.SimpleMethod | Descartar | testa o mangler do stage0 |
| ManglerGenericMethod.MethodNoParams | Descartar | testa o mangler do stage0 |
| ManglerDemangle.SimpleFunction | Descartar | testa o mangler do stage0 |
| ManglerDemangle.LongerFunction | Descartar | testa o mangler do stage0 |
| ManglerDemangle.Method | Descartar | testa o mangler do stage0 |
| ManglerDemangle.NestedMethod | Descartar | testa o mangler do stage0 |
| ManglerDemangle.NonMangled | Descartar | testa o mangler do stage0 |
| ManglerDemangle.GenericStruct | Descartar | testa o mangler do stage0 |
| Object.TypeInfoStructDefinition | Portar | exit code 1 |
| Object.ObjectStructDefinition | Portar | exit code 0 |
| Object.TypeInfoKindInt | Portar | exit code 0 |
| Object.TypeInfoKindFloat | Portar | exit code 1 |
| Object.TypeInfoKindPointer | Bloqueado | `object` e `...args` com `str` (esperado 5, veio 2) |
| Object.VariadicMethodCompiles | Portar | exit code 0 |
| Object.VariadicEmptyArgs | Portar | exit code 0 |
| Object.VariadicArgsLength | Portar | exit code 3 |
| Object.VariadicWithNormalParams | Portar | exit code 2 |
| Object.VariadicAccessTypeInfo | Portar | exit code 4 |
| Object.SyncPrintIntViaVariadic | Portar | exit code 42 |
| Object.BoxingIRContainsKind | Reescrever | confere IR; depende de `ir_contains` |
| Object.AutoBoxPrimitiveToObject | Portar | exit code 0 |
| Object.AutoBoxStringToObject | Bloqueado | `object` e `...args` com `str` (esperado 5, veio 2) |
| Object.AutoBoxStructToObject | Portar | exit code 8 |
| Object.AutoBoxStructToObjectKind | Portar | exit code 3 |
| Object.AutoBoxFloatToObject | Portar | exit code 1 |
| Object.IsExpressionWithAutoBoxedPrimitive | Portar | exit code 1 |
| Object.IsExpressionBindingWithAutoBoxed | Portar | exit code 99 |
| Object.NoWarningOnLiteralBoxing | Portar | compila sem erros |
| Object.WarningOnVariableBoxing | Bloqueado | `object` e `...args` com `str` |
| Object.WarningOnStructBoxing | Bloqueado | `object` e `...args` com `str` |

## control_flow

| Teste | Decisão | Motivo |
|---|---|---|
| ControlFlow.IfStatementTrueBranch | Portar | exit code 1 |
| ControlFlow.IfStatementFalseBranch | Portar | exit code 0 |
| ControlFlow.IfElseStatementTrueBranch | Portar | exit code 1 |
| ControlFlow.IfElseStatementFalseBranch | Portar | exit code 2 |
| ControlFlow.IfElseIfElseStatement | Portar | exit code 2 |
| ControlFlow.NestedIfStatements | Portar | exit code 1 |
| ControlFlow.IfWithEquality | Portar | exit code 1 |
| ControlFlow.IfWithNotEqual | Portar | exit code 1 |
| ControlFlow.IfWithLogicalAnd | Portar | exit code 1 |
| ControlFlow.IfWithLogicalOr | Portar | exit code 1 |
| ControlFlow.WhileLoopSimple | Portar | exit code 5 |
| ControlFlow.WhileLoopSum | Portar | exit code 15 |
| ControlFlow.WhileLoopWithBreak | Portar | exit code 5 |
| ControlFlow.WhileLoopWithContinue | Portar | exit code 9 |
| ControlFlow.WhileLoopNeverExecutes | Portar | exit code 10 |
| ControlFlow.DoWhileLoopSimple | Portar | exit code 5 |
| ControlFlow.DoWhileExecutesAtLeastOnce | Portar | exit code 11 |
| ControlFlow.DoWhileWithBreak | Portar | exit code 3 |
| ControlFlow.ForLoopSimple | Portar | exit code 5 |
| ControlFlow.ForLoopPostfix | Portar | exit code 5 |
| ControlFlow.ForLoopSum | Portar | exit code 55 |
| ControlFlow.ForLoopWithBreak | Portar | exit code 5 |
| ControlFlow.ForLoopWithContinue | Portar | exit code 9 |
| ControlFlow.ForLoopNested | Portar | exit code 9 |
| ControlFlow.RangeForSimple | Portar | exit code 5 |
| ControlFlow.RangeForSum | Portar | exit code 55 |
| ControlFlow.RangeForWithBreak | Portar | exit code 5 |
| ControlFlow.RangeForWithContinue | Portar | exit code 9 |
| ControlFlow.RangeForNested | Portar | exit code 12 |
| ControlFlow.RangeForAnonymous | Bloqueado | `for` com range |
| ControlFlow.RangeForAnonymousNested | Bloqueado | `for` com range |
| ControlFlow.RangeForVariableAccess | Portar | exit code 6 |
| ControlFlow.RangeForInclusiveEnd | Portar | exit code 6 |
| ControlFlow.RangeForInclusiveEndSum | Portar | exit code 55 |
| ControlFlow.RangeForBracketClosed | Portar | exit code 5 |
| ControlFlow.RangeForBracketHalfOpen | Portar | exit code 4 |
| ControlFlow.RangeForBracketClosedSum | Portar | exit code 55 |
| ControlFlow.RangeForAnonymousInclusive | Bloqueado | `for` com range |
| ControlFlow.RangeForAnonymousBracketClosed | Bloqueado | `for` com range |
| ControlFlow.SwitchStatementCase1 | Portar | exit code 10 |
| ControlFlow.SwitchStatementCase2 | Portar | exit code 20 |
| ControlFlow.SwitchStatementDefault | Portar | exit code 0 |
| ControlFlow.SwitchStatementWithBreak | Portar | exit code 10 |
| ControlFlow.SwitchStatementWithoutDefault | Portar | exit code 10 |
| ControlFlow.FibonacciIterative | Portar | exit code 55 |
| ControlFlow.FactorialIterative | Portar | exit code 120 |
| ControlFlow.NestedLoopsWithBreak | Portar | exit code 15 |
| ControlFlow.WhileWithMultipleConditions | Portar | exit code 10 |
| ErrorHandling.ThrowWithTryFallback | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.TryReturnsValueOnSuccess | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.BuiltinExceptionAvailableWithoutStd | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.RethrowsPropagation | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.UncheckedThrowingCallInThrowingCallerPropagates | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.DerivedErrorAcceptedByBaseThrowsClause | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.DerivedErrorAcceptedByIntermediateBase | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.ErrorAliasChain | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.DivisionByZeroThrowsInThrowingFunction | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.InterpolatedErrorMessage | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.ConsoleFormatBuildsString | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.MissingTryDiagnostic | Bloqueado | tipos de erro em `throws` (o erro atual tem outro motivo) |
| ErrorHandling.ThrowOutsideThrowsDiagnostic | Bloqueado | tipos de erro em `throws` (o erro atual tem outro motivo) |
| ErrorHandling.ThrowsTypeMismatchDiagnostic | Bloqueado | tipos de erro em `throws` (o erro atual tem outro motivo) |
| ErrorHandling.ThrowNonErrorTypeDiagnostic | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.TryOnNonThrowingDiagnostic | Portar | erro E9004 |
| ErrorHandling.TryWithoutFallbackOutsideThrowsDiagnostic | Bloqueado | tipos de erro em `throws` (o erro atual tem outro motivo) |
| ErrorHandling.ConstexprCallThatAlwaysThrowsIsCompileError | Bloqueado | tipos de erro em `throws` (o erro atual tem outro motivo) |
| ErrorHandling.ConstexprCallInsideTryIsSilentAndTakesFallback | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.ConstexprCallWithRuntimeArgumentsStaysRuntime | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.ConstexprCallThatSucceedsHasNoDiagnostics | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnThrowingCallSuccessArm | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnThrowingCallWildcardSuccess | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnThrowingCallSpecificErrorArm | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnThrowingCallGenericErrorArm | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnThrowingCallSpecificArmBeforeErrorArm | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnThrowingCallDerivedErrorMatchesBaseArm | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnThrowingCallNoArmMatchesPropagates | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnThrowingCallUnhandledAbortsOutsideThrows | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchOnConstexprThrowingCallTakesErrorArm | Bloqueado | tipos de erro em `throws` |
| ErrorHandling.SwitchArmsTypeMismatchDiagnostic | Bloqueado | tipos de erro em `throws` (o erro atual tem outro motivo) |
| ErrorHandling.SwitchBlockArmReturnDiagnostic | Bloqueado | `switch` com bloco e `yield` (o erro atual tem outro motivo) |
| ErrorHandling.SwitchOnThrowingCallBlockArmSuccessContinues | Bloqueado | `switch` com bloco e `yield` |
| ErrorHandling.SwitchBlockArmWithoutDivergenceDiagnostic | Bloqueado | `switch` com bloco e `yield` (o erro atual tem outro motivo) |
| ErrorHandling.SwitchBlockArmYieldContinuesAfterSwitch | Bloqueado | `switch` com bloco e `yield` |
| ErrorHandling.SwitchBlockArmYieldAfterStatements | Bloqueado | `switch` com bloco e `yield` |
| ErrorHandling.YieldWithValueOutsideSwitchArmDiagnostic | Bloqueado | `switch` com bloco e `yield` (o erro atual tem outro motivo) |
| RuntimeErrors.TrappedOverflowReportHasLocationValuesAndTrace | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |
| RuntimeErrors.DivisionByZeroReportShowsOperands | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |
| RuntimeErrors.NegateOverflowReport | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |
| RuntimeErrors.VariableHistoryShowsLastAssignments | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |
| RuntimeErrors.StackTraceShowsIntermediateFrames | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |
| RuntimeErrors.ShadowStackCallsAppearInIr | Reescrever | confere IR; depende de `ir_contains` |
| RuntimeErrors.NormalRunHasEmptyReport | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |
| RuntimeErrors.UncaughtExceptionEscapingMainReportsAndAborts | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |
| RuntimeErrors.UncaughtExceptionReleaseReportHasNoStackTrace | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |
| RuntimeErrors.ReleaseModeOmitsShadowStackAndVarTracking | Reescrever | confere IR; depende de `ir_contains` |
| RuntimeErrors.ReleaseModeTrapUsesMinimalCall | Reescrever | confere IR; depende de `ir_contains` |
| RuntimeErrors.ReleaseModeTrapReportKeepsLocationAndValues | Reescrever | confere o texto do relatório de erro, que no djinnc não mostra operandos nem histórico |

## functions

| Teste | Decisão | Motivo |
|---|---|---|
| Constructor.StackBasicFieldAccess | Portar | exit code 10 |
| Constructor.StackSecondField | Portar | exit code 20 |
| Constructor.StackFieldExpression | Portar | exit code 42 |
| Constructor.StackThreeFields | Portar | exit code 6 |
| Constructor.StackPassedToFunction | Portar | exit code 99 |
| Constructor.HeapBasicFieldAccess | Portar | exit code 1 |
| Constructor.HeapSecondField | Portar | exit code 69 |
| Constructor.HeapFieldExpression | Portar | exit code 42 |
| Constructor.HeapThreeFields | Portar | exit code 60 |
| Constructor.HeapFieldAssignment | Portar | exit code 77 |
| Constructor.StackAndHeapSameStruct | Portar | exit code 50 |
| Constructor.GenericStackBasic | Portar | exit code 42 |
| Constructor.GenericStackNoArgs | Portar | exit code 0 |
| Constructor.GenericStackTwoFields | Portar | exit code 42 |
| Constructor.GenericStackMethodCall | Portar | exit code 77 |
| Constructor.GenericStackMethodForwardReference | Portar | exit code 2 |
| Constructor.GenericHeapBasic | Portar | exit code 99 |
| Constructor.GenericHeapMethodCall | Portar | exit code 55 |
| Method.BasicMethodWithBlock | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Method.ExpressionBodyArrow | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Method.MethodWithParameters | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Method.PublicPrivateModifiers | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Method.StaticMethod | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Method.VoidMethodWithBlock | Portar | compila sem erros |
| Method.MixedFieldsAndMethods | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| OperatorOverload.ParseOperatorInInterface | Portar | compila sem erros |
| OperatorOverload.ParseOperatorInImpl | Portar | compila sem erros |
| OperatorOverload.StructAddition | Portar | exit code 10 |
| OperatorOverload.StructEquality | Portar | exit code 1 |
| OperatorOverload.NotEqualDerivedFromEqual | Portar | exit code 42 |
| OperatorOverload.BitwiseOperatorsOnPrimitives | Portar | exit code 40 |
| OperatorOverload.OperatorInStructBody | Portar | exit code 42 |
| OperatorOverload.MultipleOperatorsOnSameStruct | Portar | exit code 99 |

## generics

| Teste | Decisão | Motivo |
|---|---|---|
| GenericConstraints.ParseWhereClauseSingleConstraint | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| GenericConstraints.ParseWhereClauseMultipleConstraintsSameParam | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| GenericConstraints.ParseWhereClauseMultipleParams | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| GenericConstraints.ParseWhereClauseOnEnum | Bloqueado | o djinnc rejeita: an enum cannot carry a payload: declare it as a union |
| GenericConstraints.ParseWhereClauseOnInterface | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| GenericConstraints.UndefinedConstraintInterfaceError | Bloqueado | o djinnc não reproduz o comportamento |
| GenericConstraints.ConstraintSatisfied | Portar | exit code 0 |
| GenericConstraints.ConstraintViolated | Portar | erro E3050 |
| GenericConstraints.NoConstraintNoValidation | Portar | exit code 0 |
| GenericConstraints.MultipleConstraintsSatisfied | Portar | exit code 0 |
| GenericConstraints.MultipleConstraintsPartiallyViolated | Portar | erro E3050 |
| GenericConstraints.EnumConstraintSatisfied | Bloqueado | o djinnc rejeita: an enum cannot carry a payload: declare it as a union |
| GenericConstraints.EnumConstraintViolated | Bloqueado | enum genérico com restrição (o erro atual tem outro motivo) |
| GenericConstraints.WhereClauseWithImplements | Portar | exit code 1 |
| FullCompilation.StructGeneric | Portar | exit code 0 |
| GenericStruct.TwoParameters | Portar | exit code 0 |
| GenericStruct.ThreeParameters | Portar | exit code 0 |
| GenericStruct.FourParameters | Portar | exit code 0 |
| GenericStruct.MapEntry | Portar | exit code 0 |
| GenericStruct.TwoParametersWithInit | Portar | exit code 0 |
| GenericStruct.TwoParametersFieldAccess | Portar | exit code 42 |
| GenericStruct.TwoParametersSecondFieldAccess | Portar | exit code 99 |
| GenericStruct.MixedTypes | Portar | exit code 50 |
| GenericStruct.PointerTypes | Portar | exit code 0 |
| GenericStruct.MultipleInstantiations | Portar | exit code 0 |
| GenericStruct.NestedGenericStruct | Portar | exit code 0 |
| GenericStruct.FunctionReturningGenericStruct | Portar | exit code 15 |
| GenericStruct.GenericStructAsParameter | Portar | exit code 77 |
| GenericStruct.ThreeParametersFieldSum | Portar | exit code 60 |
| Interface.BasicDefinition | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Interface.SameNameInterfaceDefinition | Bloqueado | o djinnc rejeita: expected '{' (a function without a body is only allowed in [intrinsic] and [no_mangle] structs) |
| Interface.GenericInterface | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Interface.StructImplementsInterface | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Interface.StructImplementsMultipleInterfaces | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |

## hello_world

| Teste | Decisão | Motivo |
|---|---|---|
| Hello.World | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |

## memory

| Teste | Decisão | Motivo |
|---|---|---|
| Mutable.BasicMutableVariables | Portar | exit code 2 |
| Mutable.InvalidImmutableAssingment | Portar | erro E3042 |
| Ownership.CopyTypesDoNotMove | Portar | exit code 84 |
| Ownership.FloatCopyTypes | Portar | exit code 1 |
| Ownership.PointerCopyTypes | Portar | exit code 20 |
| Ownership.StructMoveSemantics | Portar | exit code 100 |
| Ownership.UseAfterMove | Portar | erro E5001 |
| Ownership.DoubleMoveError | Portar | erro E5001 |
| Ownership.ReinitializeAfterMove | Portar | exit code 30 |
| Ownership.ScopeOwnership | Portar | exit code 10 |
| Ownership.NestedScopeOwnership | Portar | exit code 3 |
| Ownership.FunctionParameterCopyType | Portar | exit code 21 |
| Ownership.IfStatementOwnership | Portar | exit code 10 |
| Ownership.WhileLoopOwnership | Portar | exit code 10 |
| Ownership.ForLoopOwnership | Portar | exit code 10 |

## modules

| Teste | Decisão | Motivo |
|---|---|---|
| Extern.PrintfHelloWorld | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Extern.BlockSyntax | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Extern.MultipleExternBlocks | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Extern.StdImportNoDuplicateExternSymbols | Reescrever | confere IR; depende de `ir_contains` |
| Extern.ExternNotRemovedByOptimizer | Reescrever | confere IR; depende de `ir_contains` |
| Import.BasicImport | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Import.QualifiedImport | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Import.MultipleImports | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Import.ImportParsesQualifiedName | Portar | compila sem erros; as asserções sobre a AST do stage0 não valem para o djinnc |
| Namespace.BasicDefinition | Portar | exit code 0 |
| Namespace.StructInNamespace | Portar | exit code 0 |
| Namespace.NestedNamespace | Portar | exit code 0 |
| Namespace.MultipleFunctionsInNamespace | Portar | exit code 0 |
| Namespace.StructAndFunctionInNamespace | Portar | exit code 0 |
| Namespace.FileScopedNamespace | Portar | exit code 0 |
| Namespace.FileScopedQualifiedNamespace | Portar | exit code 0 |
| Namespace.NoNamespaceIsGlobal | Portar | exit code 0 |

## strings

| Teste | Decisão | Motivo |
|---|---|---|
| StringInterpolation.SimpleIdentifier | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.MultipleArgs | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.MixedWithExplicitPositional | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.NoInterpolationLeavesStringUnchanged | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.FieldAccessInsideBraces | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.BlockStringTripleQuoted | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.BlockStringNoEscapes | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.ArithmeticExpressionInsideBraces | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.FunctionCallExpandsAsArgument | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.RepeatedExpressionEvaluatedTwice | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.EmptyBracesPreserved | Reescrever | confere IR; depende de `ir_contains` |
| StringInterpolation.NonCallStringLiteralNotInterpolated | Reescrever | confere IR; depende de `ir_contains` |

## types

| Teste | Decisão | Motivo |
|---|---|---|
| AddressOf.StructLocalComparesWithItsPointer | Portar | exit code 7 |
| AddressOf.IntegerLocalDereferencesToItsValue | Portar | exit code 5 |
| Cast.IntTruncation | Portar | exit code 42 |
| Cast.IntWidening | Portar | exit code 7 |
| Cast.FloatToInt | Portar | exit code 3 |
| Cast.IntToFloat | Portar | exit code 5 |
| Cast.ChainedCast | Portar | exit code 100 |
| Cast.SameTypeCastNoop | Portar | exit code 99 |
| Cast.CastInExpression | Portar | exit code 13 |
| Cast.FloatTruncation | Portar | exit code 2 |
| Cast.PointerCast | Portar | exit code 42 |
| Cast.CastLiteral | Portar | exit code 3 |
| Cast.NegativeFloatToInt | Portar | exit code 9 |
| ConstField.BasicConstFieldStaticAccess | Portar | exit code 100 |
| ConstField.ConstFieldUsedInStaticMethod | Portar | exit code 0 |
| ConstField.ConstFieldWithPublicModifier | Portar | exit code 42 |
| ConstField.ConstFieldDoesNotAffectStructLayout | Portar | exit code 20 |
| ConstField.ConstFieldAccessViaInstance | Bloqueado | o djinnc rejeita: struct has no such field |
| ConstField.ConstFieldNegativeValue | Portar | exit code 9 |
| ConstField.MutAndConstError | Bloqueado | o djinnc não reproduz o comportamento |
| ConstField.ConstFieldWithoutInitializerError | Portar | erro E2001 |
| ConstField.MultipleConstFields | Portar | exit code 3 |
| ConstField.ConstFieldWithMixedRegularFields | Portar | exit code 100 |
| IsExpression.ObjectIsI32True | Portar | exit code 1 |
| IsExpression.ObjectIsI32False | Portar | exit code 0 |
| IsExpression.ObjectIsF64 | Portar | exit code 1 |
| IsExpression.ObjectIsStr | Bloqueado | `object` e `...args` com `str` (esperado 1, veio 0) |
| IsExpression.NegatedIsExpression | Portar | exit code 1 |
| IsExpression.ErrorOnNonObjectOperand | Portar | erro E3036 |
| IsExpression.MultipleTypeChecks | Bloqueado | `object` e `...args` com `str` (esperado 3, veio 0) |
| IsExpression.IRContainsIsCheck | Reescrever | confere IR; depende de `ir_contains` |
| IsExpression.BindingExtractsI32Value | Portar | exit code 42 |
| IsExpression.BindingNotUsedWhenTypeMismatch | Portar | exit code -1 |
| IsExpression.BindingWithDifferentTypes | Portar | exit code 99 |
| NativeTypes.DeclarationsCompile | Portar | compila sem erros |
| NativeTypes.NintIsPointerSized | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| NativeTypes.NfloatIsF32 | Reescrever | confere IR; depende de `ir_contains` |
| NativeTypes.NdoubleIsF64 | Reescrever | confere IR; depende de `ir_contains` |
| NativeTypes.NintMixesWithI64 | Portar | compila sem erros |
| NativeTypes.TypeofShowsNativeName | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| NativeTypes.NintMaxValueConstant | Bloqueado | tipos nativos (`nint`) |
| NativeTypes.NintTakesOverflowSuffixes | Bloqueado | tipos nativos (`nint`) |
| NativeTypes.NfloatTypeofShowsNativeName | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| NativeTypes.NdoubleTypeofShowsNativeName | Bloqueado | `sizeof`/`typeof`/`alignof` de expressão |
| NativeTypes.NintSaturatingArithmetic | Bloqueado | tipos nativos (`nint`) |
| NativeTypes.NintNegation | Portar | exit code 5 |
| NativeTypes.NintAsFunctionSignature | Portar | exit code 42 |
| NativeTypes.NfloatNdoubleArithmetic | Portar | compila sem erros |
| NonZero.DeclarationsCompile | Portar | compila sem erros |
| NonZero.SameRepresentationAsBaseType | Reescrever | confere IR; depende de `ir_contains` |
| NonZero.RejectsZeroLiteral | Bloqueado | tipos non-zero (`i32n`) |
| NonZero.RejectsZeroLiteralVariants | Bloqueado | tipos non-zero (`i32n`) |
| NonZero.RejectsImplicitConversionFromPlainType | Bloqueado | tipos non-zero (`i32n`) |
| NonZero.ExplicitCastIsTrusted | Portar | compila sem erros |
| NonZero.RejectsCastOfZeroLiteral | Bloqueado | tipos non-zero (`i32n`) |
| NonZero.ArithmeticResultLosesGuarantee | Bloqueado | tipos non-zero (`i32n`) |
| NonZero.RejectsIncrementAndDecrement | Bloqueado | tipos non-zero (`i32n`) |
| NonZero.RejectsDeclarationWithoutInitializer | Bloqueado | tipos non-zero (`i32n`) |
| NonZero.CombinesWithOverflowSuffix | Portar | compila sem erros |
| NonZero.RejectsSuffixOnFloatType | Bloqueado | modos de overflow (o erro atual tem outro motivo) |
| NonZero.DivisionByNonZeroRuns | Portar | exit code 10 |
| NonZero.DivisionByConstantSkipsZeroCheck | Reescrever | confere IR; depende de `ir_contains` |
| NonZero.DivisionByUnknownStillChecked | Reescrever | confere IR; depende de `ir_contains` |
| NonZero.DivisionByNonZeroParamSkipsCheckAndChecksEntry | Reescrever | confere IR; depende de `ir_contains` |
| NonZero.RequireNonZeroDedupsEntryCheck | Bloqueado | contratos (`require`/`ensure`) |
| NonZero.RequireProvesParamNonZeroForAssignments | Bloqueado | contratos (`require`/`ensure`) |
| NonZero.CompileTimeViolationForNonZeroParam | Bloqueado | tipos non-zero (`i32n`) |
| NonZero.CompileTimeViolationForRequireStillWorks | Bloqueado | contratos (`require`/`ensure`) (o erro atual tem outro motivo) |
| NonZero.NonZeroParamCallWithNonZeroConstantCompiles | Portar | compila sem erros |
| NonZero.EnsureProvesNonZeroReturn | Bloqueado | contratos (`require`/`ensure`) |
| NonZero.EnsureWorksInAnyDeclarationOrder | Bloqueado | contratos (`require`/`ensure`) |
| NonZero.EntryCheckTrapsAtRuntime | Portar | exit code diferente de zero |
| Nullable.NullLiteralAllowedOnNullableType | Portar | compila sem erros |
| Nullable.NullLiteralForbiddenOnNonNullableStruct | Portar | erro E3053 |
| Nullable.NullLiteralForbiddenOnNonNullablePointer | Portar | erro E3053 |
| Nullable.NullLiteralForbiddenOnInteger | Portar | erro E3053 |
| Nullable.NullableArraySyntax | Bloqueado | tipos anuláveis |
| Nullable.ReassignNullToNullableMutVariable | Portar | compila sem erros |
| Nullable.ReassignNullToNonNullableErrors | Portar | erro E3053 |
| Nullable.NullCoalescingParsesAndCompiles | Bloqueado | tipos anuláveis |
| Nullable.NullCoalescingAssignmentDesugars | Bloqueado | tipos anuláveis |
| Nullable.NullConditionalFieldAccessParses | Bloqueado | tipos anuláveis |
| Nullable.NullForgivingFieldAccessParses | Bloqueado | tipos anuláveis |
| Nullable.CastNonNullableToNullableAllowed | Bloqueado | tipos anuláveis |
| Nullable.CastNullableToNonNullableEmitsWarning | Bloqueado | tipos anuláveis (o erro atual tem outro motivo) |
| Nullable.CastNullableToNullableNoWarning | Portar | compila sem erros |
| Nullable.CastIntegerToPointerNoWarning | Portar | compila sem erros |
| PointerCompat.VoidToTyped | Portar | compila sem erros |
| PointerCompat.TypedToVoid | Portar | compila sem erros |
| PointerCompat.IncompatibleTypes | Bloqueado | conversão entre ponteiros incompatíveis (o erro atual tem outro motivo) |
| PointerCompat.IncompatibleTypesHumanReadableMessage | Portar | compila sem erros |
| PointerCompat.SameType | Portar | compila sem erros |
| PointerField.ComparesWithLocalInsideNamespace | Portar | exit code 1 |
| UnsignedCompare.EqualsConstantAboveI32Max | Bloqueado | `const` local |
| UnsignedCompare.OrderingUsesUnsignedPredicates | Portar | exit code 1 |
| UnsignedCompare.CastWidensWithZeroExtension | Portar | exit code 1 |
| UnsignedCompare.SignedStillOrdersNegatives | Portar | exit code 1 |
