# Roadmap: sistema de testes nativo do djinnc

Objetivo: substituir os testes em C++ (gtest, legado) e os scripts de shell por um sistema escrito em Djinn, em que os
testes formam um grafo e a saída de um teste pode ser a entrada de outro. Um e2e passa a reaproveitar o que já foi
validado em vez de repetir tudo. É o primeiro passo para o djinnc virar um SDK (`djinnc test`, `djinnc bench`, ...).

## Estado atual

| Item | Situação |
|---|---|
| gtest (`tests/**/*.cpp`) | 724 testes, ~6 s cada: cada um compila o std e linka com clang. Só 4 arquivos usam internos do stage0. |
| O que os gtests verificam | `returnCode` (474 usos), `diagnostics` (607), IR (73), AST (~70), relatório de runtime (9). |
| Código de erro nos gtests | Só 6 usos de `hasErrorCode`. O resto conta diagnósticos, então não sabe *qual* erro saiu. |
| `tests/djinnc/run.sh` | Samples com `.exit` e `.stdout` opcional. Erros: só faz `grep "error:"`. Sequencial, sem stdin. |
| `djinnc --selftest` | Runner manual (`SelfTest.check`) para arena, atoms e mapas. |
| `tools/bootstrap.sh`, `tests/djinnc/library/run.cmd` | Cadeias em que um passo consome o artefato do anterior, escritas à mão. |

Lacunas do djinnc que condicionam o desenho (verificadas no código):

- **Diagnósticos sem estrutura.** `Diagnostics` ([diagnostics.djinn](src/support/source/diagnostics.djinn)) só imprime
  texto: não tem código nem `length`, e nada fica guardado. São 155 pontos de erro e 7 de warning. O stage0 tem 54 códigos.
- **Processo sem captura.** `Process.run` é `system()`: sem stdout/stderr separados, stdin, timeout ou espera paralela.
- **Sem paralelismo real dentro do processo.** `spawn` enfileira em `ready_queue`, drenada pelo event loop; nada alimenta
  `pool.queue`, então as 4 threads do pool ficam ociosas. A shadow stack também é global, não por thread.
- **Falha mata o processo.** `__djinn_panic` chama `abort()` e `Debug.assert` chama `debugtrap()`.
- **Sem closures** (proibidas no subset). `typeof(function)` já devolve `MethodInfo*` com nome, tipos e ponteiro.
- **Sem descoberta global.** `Type#methods` lista um tipo, mas nada lista "todas as funções com `[Test]`".
- **Biblioteca.** `.djlib` não declara dependências, e globais de header são só declarações (sem inicializador estático).

## Desenho

### Quem faz o quê

O compilador oferece mecanismos; o framework os consome. O scheduler não vive no compilador, porque o djinnc compila sem
std (`libs: []`) e o framework precisa dela.

```
djinnc (compilador)            diagnósticos em JSON, tabela de [Test], subcomando `djinnc test`
libs/testing (.djlib)          nós, grafo, scheduler, processos, asserts, relatórios   -> para todo mundo
djinnc/testing (sublib)        casos .djinn, [Requires], diagnósticos esperados, IR    -> só para testar o compilador
projetos de teste              tests/conformance (usa as duas), projetos do usuário (só libs/testing)
```

`libs/testing` fica fora de `std/` porque tudo em `std/` entra em qualquer build com `--std`. Assim só quem importa paga.

**O usuário final não vê estágio de compilação.** Para ele um teste sempre roda no binário final: `djinnc test` compila o
projeto, linka uma vez e executa. Tudo que fala de `Check`, IR, JIT ou diagnóstico esperado mora em `djinnc/testing`, que
só `tests/conformance` importa. O que continua em `libs/testing` é testar um `.exe` de fora: rodar, passar stdin e
verificar stdout, stderr e exit code.

**O runner não pode depender do compilador que está testando.** Ele é compilado pelo seed estável e `tests/conformance`
recebe o compilador sob teste por `--compiler <path>`. Sem isso, um bug de codegen pode quebrar o runner e esconder a
própria falha. Essa flag é da sublib, não do `djinnc test`.

### Os tipos são as portas do grafo

A assinatura da função já declara o grafo. Não há DSL nova:

```djinn
import testing;

[Test]
UserAuthenticated login() {
    UserAuthenticated user = Auth.sign_in("ana", "secret");
    Expect.true(user.is_valid());
    return user;
}

[Test]
CartResult add_to_cart(UserAuthenticated user) {
    CartResult cart = Cart.add(user, 42);
    Expect.equal(cart.items, 1);
    return cart;
}

[Test]
void pay(CartResult cart) {
    Expect.equal(Payment.charge(cart).status, 200);
}
```

Regras de ligação:

- Parâmetro do tipo `T` liga ao único `[Test]` que retorna `T`.
- Nenhum produtor, dois produtores sem desempate ou ciclo são erro antes de rodar qualquer teste.
- Desempate: `[From("login_admin")]` no parâmetro, ou um workflow explícito.
- Um único validador, usado na descoberta e na API fluente. `--list` valida sem executar.

Workflow explícito, opcional, no entrypoint (`typeof(function)` substitui a closure que não existe):

```djinn
i32 main() {
    TestPlan* plan = TestPlan.discover();
    plan.workflow("checkout").step(typeof(login)).step(typeof(add_to_cart)).step(typeof(pay));
    plan.workflow("checkout_admin").step(typeof(login_admin)).step(typeof(add_to_cart)).step(typeof(pay));
    return plan.run();
}
```

Um nó é identificado por **função + ligações**. `add_to_cart` com `login` e com `login_admin` são dois nós, mas `login`
roda uma vez só para todos os workflows que o usam. É daí que vem a redução de custo.

### Resultado de um nó

`Passed`, `Failed`, `Blocked`, `Skipped`, `Timeout`, `Crashed`.

`Blocked` é separado de `Failed`: se `login` quebra, o relatório mostra 1 falha e N bloqueados, não N+1 falhas. O custo do
modelo é que uma raiz quebrada tira a cobertura de tudo abaixo dela, e o relatório precisa deixar isso visível.

### Execução

Valores em memória não atravessam processos e um crash derruba o processo. Por isso a unidade de agendamento é o
**componente conexo** do grafo:

- Componentes independentes rodam em paralelo, um processo worker por componente (`--jobs N`).
- Dentro de um componente a execução é cooperativa: ramos com I/O se sobrepõem via `spawn`/`await`, ramos de CPU são sequenciais.
- Falha fatal: o worker grava o resultado e morre; o runner sobe outro para os nós restantes, reexecutando os ancestrais necessários.

Isso cobre bem os dois casos reais: testes de compilador são nós isolados (paralelismo total entre processos) e um e2e de
marketplace é limitado por I/O (a concorrência cooperativa basta).

**Limite conhecido:** `A → B` e `A → C` com B e C pesados em CPU não rodam em paralelo. Resolver isso exige uma destas
duas coisas: saídas serializáveis (a interface `Serializable` já existe) ou threads reais no runtime. Saídas que são
arquivos (`Artifact`) já cruzam processos, então `stage1 → stage2` e `lib → app` não têm esse limite.

**Estado compartilhado:** a saída é entregue como somente leitura, por contrato. O compilador ainda não garante isso:
passar para função não move, borrows em chamadas não são rastreados e escrita via `*p` é sempre permitida. Quem precisa
mutar marca o parâmetro com `[Mutates]` e recebe uma cópia nova, reexecutando o produtor. A garantia estática vem junto
com `ptr<mut T>` (já é um TODO do [SELF_HOSTING_ROADMAP.md](SELF_HOSTING_ROADMAP.md)).

### Async por padrão

Como o resto da linguagem, os testes são async por padrão. Um teste é uma task: o runner dá `spawn` nela e segue
agendando outras enquanto ela espera, então `await` de um processo, de um timer ou de I/O não trava o resto da suíte.

```djinn
[Test]
async CartResult add_to_cart(UserAuthenticated user) {
    Response response = await Http.post("/cart", user.token);
    await Task.delay(50);
    Expect.equal(response.status, 200);
    return CartResult(response);
}
```

- Funções síncronas continuam válidas (`async` é o padrão do compilador, não uma obrigação do autor), e o runner as agenda
  como tasks que terminam sem suspender.
- **Dentro de um componente do grafo**, nós independentes que esperam em I/O se sobrepõem na mesma thread. É a
  concorrência cooperativa já descrita em *Execução*; processos continuam sendo o paralelismo de CPU.
- **Timeout por nó** vira uma corrida entre a task e `Task.delay(limite)`, sem thread vigia.
- **`Verify` é async**: `await verify.runs(...)` espera o processo filho sem bloquear, então vários casos de compilador
  rodam juntos num mesmo worker.

O runtime já tem o yield que isso precisa: uma corrotina que suspende sem se marcar como esperando é reenfileirada na
`ready_queue` (`run_event_loop_core`). `Task.delay` e `Process.wait_async` usam isso, sem thread extra:

| Primitiva | Situação |
|---|---|
| `Task.delay(ms)` | Feito (`std/sys/task.djinn`): suspende até o relógio (`__djinn_unix_timestamp_ms`) passar do prazo. |
| `Task.pause()` | Feito: um yield cooperativo (`yield` é palavra reservada, por isso o nome). |
| `Process.wait_async(p, timeout_ms)` | Feito (`std/sys/process.djinn`): consulta `__djinn_process_poll` (não bloqueante) e suspende entre as consultas. `Process.wait` continua bloqueante. |
| `spawn` com threads reais | `spawn` só enfileira na `ready_queue`; as 4 threads do pool ficam sem uso. Não é pré-requisito: os processos cobrem o paralelismo de CPU. |

Limite: a espera é por polling, então o event loop gira enquanto todas as tasks esperam (já faz `sched_yield` quando a fila
está vazia). Se isso pesar, a evolução é uma thread de timers/processos que acorda a corrotina, como a fila de I/O de arquivos.
Teste: `tests/djinnc/std/async_delay.djinn`.

**Risco:** um teste que faz muito trabalho de CPU sem `await` ocupa o event loop e atrasa os outros do mesmo worker. O
relatório marca nós que passam de um limite sem suspender, e o autor move esse teste para um worker próprio com
`[Test(Runtime = Isolated)]`.

### Asserts

- `Expect.*` registra a falha e continua (`equal`, `not_equal`, `true`, `false`, `null`, `contains`, ...).
- `Assert.*` é fatal: usa o caminho de panic, com um gancho novo que grava o resultado antes do `abort()`.

Assim não é preciso `try` em toda linha nem `longjmp` através de corrotinas. O preço é um worker novo a cada falha fatal,
o que só acontece em falha. A localização vem de `[Location]`, que já funciona.

### Testes de compilador (`djinnc/testing`)

Esta seção e a seguinte valem só para a sublib. O que o teste espera é código, não comentário dentro da source, como o
`DiagnosticResult` e o `Verify` do Roslyn. A source é um `.djinn` puro em `cases/`, sem anotação: compila sozinho, abre
no editor sem ruído e pode ser reaproveitada por mais de um teste.

```djinn
[Test]
[Requires(Check)]
void overflow_is_rejected() {
    Source* source = Sources.text(OVERFLOW);
    Verify.with_source(source).diagnostics(
        Expected.error(DiagnosticErrors.INVALID_NUMBER).at(3, 13).with_message("128")
            .and_warning(DiagnosticWarnings.ATTRIBUTE_NOT_APPLIED).at_position(source.find("[Align")));
}

[Test]
[Requires(JIT)]
void arithmetic_runs() {
    Verify.source(ARITHMETIC).runs(Expected.exit(110).stdout("hello\n"));
}

[Test]
[Requires(Check)]
void small_case(Verify verify) {
    verify.compiles(Sources.text("i32 main() { return 0; }"));
}
```

- **`Sources`** cria a entrada: `file(path)` para casos em disco, `text(...)` para casos de uma linha. Nenhum dos dois
  conhece expectativas. Há opções de compilação por chamada, como `verify.with_flags("--std")`, e não em cabeçalho da source.
- **`Expected`** é um construtor encadeado: `error(code)`, `warning(code)`, `.at(line, column)`, `.with_message(parte)`
  (substring opcional) e, para execução, `exit(n)`, `stdout(...)`, `stderr(...)`, `stdin(...)`.
- **Argumentos estruturados:** `.with_arguments("BadOne")` compara os argumentos com que o diagnóstico foi formatado, não o
  texto final, como o `WithArguments` do Roslyn. Para isso o diagnóstico precisa ter um id de mensagem e uma lista de
  argumentos, e o JSON ganha `"args": ["BadOne"]`. Hoje o djinnc só guarda a mensagem já formatada; migrar cada `error(...)`
  para `error(code, "member names cannot be the same as '{0}'", name)` é parte da Fase 0 (item 1, pendente).
- **Localização completa:** `.at(linha, coluna)` e `.at_range(linha, coluna, linha_fim, coluna_fim)`; o JSON passa a
  trazer `length` real (hoje fixo em 1) e `end_line`/`end_column`.
- **Vários diagnósticos de uma vez**, no estilo `DiagnosticResult[] expected`. Hoje a lista é encadeada: `Expected.error(A).at(1, 2).and_error(B).and_warning(C)`, e `.at` e `.with_message` valem para o último da cadeia. O exemplo abaixo mostra a forma com `[Test]`, da Fase 3:

```djinn
[Test]
[Requires(Check)]
void member_named_like_type(Verify verify) {
    Source source = Sources.text("""
struct BadOne {
    void BadOne() {}
}
struct GoodOne {}
""");
    verify.diagnostics(source,
        Expected.error(DiagnosticErrors.MEMBER_SAME_AS_TYPE).at(3, 10).with_arguments("BadOne")
            .with_message("'BadOne': member names cannot be the same as their enclosing type"),
        Expected.warning(DiagnosticWarnings.UNUSED_TYPE).at(5, 8).with_arguments("GoodOne"));
}
```

- **O código é obrigatório**, e é a constante do compilador (a sublib fica no namespace `djinnc`), separada por severidade:
  `DiagnosticErrors` e `DiagnosticWarnings`. `Expected.error` só aceita o primeiro grupo e `Expected.warning` o segundo, então
  trocar a severidade por engano não passa. Renomear ou renumerar não deixa teste apodrecer em silêncio.
- **Posição:** `.at(linha, coluna)` é exato, mas quebra ao inserir uma linha no topo da source. `source.find("texto")`
  devolve a posição da primeira ocorrência e evita isso. Os dois formatos valem.
- **A comparação é exata:** um diagnóstico não previsto também reprova, e a ordem não importa. A mensagem do `Verify` lista
  o que faltou e o que sobrou, com o código e a posição de cada um.
- **IR e AST:** `verify.ir_contains(source, "sdiv")` e `verify.ast_matches(source, snapshot)`.
- **Exit code:** em POSIX compara os 8 bits baixos (a regra de `DJINN_EXIT` em `tests/TestHelpers.h`). Sinal e abort são estados próprios.
- **Sem sidecars:** `.exit`, `.stdout`, `.code`, `.expect` e `.debug` não existem mais. Toda expectativa está em `tests/conformance/suites/`.

### `[Requires]`: até onde um teste do compilador vai

Nem todo teste do compilador precisa de binário final. Cada teste declara o que precisa, e o link com clang fica só para
quem testa o próprio link:

| Valor | Para em | Usado quando o teste verifica | Flag do djinnc |
|---|---|---|---|
| `Check` | análise semântica | diagnósticos de lexer, parser e semântica | `--check` (novo) |
| `IR` | geração de IR | `ir-contains`, snapshot de IR, diagnósticos `E4xxx` | `--no-link` |
| `JIT` | execução via ORC, sem objeto nem link | exit code, stdout, stdin | `--run --runtime-lib` |
| `Link` | executável real | `.djlib`, `-g` e símbolos de crash, flags de linker, bootstrap | padrão |

Os valores são uma máscara, não uma escada, porque `JIT` e `Link` são caminhos paralelos depois do IR. `IR | JIT` verifica
o IR e executa; `JIT | Link` executa pelos dois caminhos e exige o mesmo resultado, que é a forma explícita de pegar
divergência entre JIT e AOT.

O atributo fica no método de teste, nunca na source. Assim a source continua sendo só a entrada sob teste, e um caso de
erro de parser não depende de o atributo ser lido. Sem `[Requires]`, a máscara é deduzida das chamadas do método
(`diagnostics` sem execução pede `Check`; `runs` pede `JIT`; `ir_contains` pede `IR`), o que cobre a maior parte dos casos.

- Pedir menos do que o método exige é erro: um teste que chama `runs` não aceita `[Requires(Check)]`.
- Os valores ficam em `TestPath`: `[Requires(TestPath.JIT | TestPath.LINK)]`. O atributo é lido do próprio método `[Test]`.
- `--require Link` só vale para testes que executam código. `compiles` e `diagnostics` não avançam, porque muitas sources
  desses grupos não têm `main`. Com `--require Link`, um teste de `runs` passa pelo JIT e pelo link.
- Hoje `generateBinary = true` aparece em 466 dos 724 gtests. A maioria só olha o exit code, então cabe em `JIT`.
- **Nem todo erro sai na semântica.** `ReflectionDiagnostics.check` e "no 'main' function defined" são emitidos no
  `CodeGenerator`. Por isso a dedução usa a categoria do código (`E4xxx` vai para `IR`), e esses pontos podem subir para a semântica depois.
- **`JIT` não é idêntico a AOT**: o handler de crash, a simbolização e a entrada de `main` diferem. `--require Link` em
  `tests/conformance` força tudo até o link, para o CI noturno, sem pesar no ciclo do dia a dia.
- `--run` exige `--runtime-lib`, então o runtime é compilado uma vez por execução da suíte, não por caso.
- **`[Requires(IR | JIT)]` funciona** (Fase 0, item 7): o argumento é uma expressão constante checada contra o `attribute`
  declarado e chega à reflexão como um payload tipado.

### `[TestPolicy]`: repetição e timeout por teste

O sistema de testes não depende do compilador: o atributo é declarado em `libs/testing/src/attributes.djinn` como qualquer
`attribute` de usuário, e o runner o identifica pela reflexão. Nem o djinnc nem a std conhecem esse nome.

```djinn
[Test]
[TestPolicy(2, 5000)]
void flaky_network() {
    Verify.source(NETWORK).runs(Expected.exit(0));
}
```

- `retry`: quantas vezes o caso roda de novo se o processo morrer com exit code negativo. O padrão é 0, então um crash falha na
  primeira vez. Falha de asserção ou exit code errado não repete; casos de crash também não, porque ali morrer é o esperado.
- `timeout`: limite em milissegundos, no lugar dos 120 s do runner.
- A source do caso não leva o atributo: ela continua sendo só a entrada sob teste.
- Em um método com `[Test]`, o atributo é lido do próprio método. Em um teste registrado à mão, a função é só o portador.

## Etapas

Cada etapa entrega algo usável sozinha. As Fases 2 e 3 são independentes e podem andar em paralelo.

### Fase 0: fundações no djinnc

1. **Diagnósticos estruturados.** Struct `Diagnostic` (severidade, código, arquivo, linha, coluna, length, mensagem,
   token encontrado), acumulada em vez de só impressa. Mesmo esquema do stage0 (`E` + `XYYY`; 1 lexer, 2 parser,
   3 semântica, 4 codegen). Migrar os 162 pontos por área. O texto vira `error[E2005]:`, então o `grep "error:"` do
   `run.sh` precisa mudar junto.
2. **`--diagnostics-format=json`**, uma linha JSON por diagnóstico:
   `{"severity":"error","code":"E2005","file":"a.djinn","line":3,"column":11,"length":1,"message":"...","found":";"}`
3. **Processo com captura.** `__djinn_process_spawn` no runtime (CreateProcess e posix_spawn) com argv, stdin, stdout e
   stderr separados, timeout e `wait_any`; exposto em `std/sys/process.djinn`.
4. **`--check`**: para depois da análise semântica, sem codegen nem link, para testes de front-end.
5. **`--run` robusto para testes**: conferir stdin, stdout, exit code e panic através do JIT, que hoje quase não é exercitado pela suíte.
6. **Medir o custo por estágio** (`Check`, `IR`, `JIT`, `Link` com clang e com `--lld`) para confirmar a economia e calibrar `--jobs`.
7. **Argumentos de atributo não literais.** `parse_attributes` guarda o texto do argumento (`"IR | JIT"`) em vez de
   descartar. A sublib interpreta esse texto, como `AttributeInfo` já entrega valores em texto. Não vira erro: argumentos
   nomeados (`message = "x"`) dependem de o parser ignorar o que não é literal.

#### Estado da Fase 0 (compilada e testada no Windows)

Verificado com o seed `build/release/djinnc-windows-x64.exe`: o djinnc novo compila, `tests/djinnc/run.sh` passa
(146 casos, `DJINN_USE_LLD=1` e o runtime reconstruído com `tools/build_runtime.sh`) e `fixpoint.sh` dá `PASS fixpoint`.
Linux e o caminho POSIX do runtime de processos não foram testados.

| Item | Situação |
|---|---|
| 1. Diagnósticos com código | Feito. `Diagnostics` recebe `code` em todos os métodos e o texto virou `file:l:c: error[E2001]: msg`. [diagnostic_code.djinn](src/support/source/diagnostic_code.djinn) tem `DiagnosticErrors` (3001-3052 semântica, 4001-4003 codegen, 5001-5006 ownership, 8xxx atributos, 9xxx tratamento de erros) e `DiagnosticWarnings`. Todo erro do checker, do analyzer e do codegen usa um código específico; o genérico `E3000`/`E4000` só sobra como padrão de `error(Node*, msg)`. |
| 2. `--diagnostics-format=json` | Feito. Uma linha por diagnóstico, com `code` (`"E3002"`), `line`, `column`, `length` (o tamanho do token do nó) e, onde o erro nomeia algo, `args`: `{"code":"E3002","length":12,"message":"undefined variable","args":["missing_name"]}`. Hoje `args` existe para variável indefinida, campo desconhecido e função não declarada; os demais erros ainda levam a informação só na mensagem. |
| 3. Processo com captura | Feito no Windows: `__djinn_process_*` no runtime e `Process.start/wait/kill/...` no std. Amostra `tests/djinnc/std/process_spawn.djinn` roda `clang` e confere stdout, stderr e exit code. `wait_any` existe só no runtime. `spawn` é palavra reservada, então o wrapper se chama `start`. |
| 4. `--check` | Feito. Não cobre erros que hoje só saem na geração. |
| 5. `--run` para testes | Corrigido. No Windows falhava com *incompatible data layouts* (o módulo usava layout manual) e com `write` não resolvido (o JIT não enxerga o `write` legado). Agora o módulo adota o layout do JIT e `write` vira `_write`. Dos 94 samples e testes do std, **todos** batem em exit code e stdout pelo JIT. |
| 6. Custo por estágio | Medido, ver abaixo. |
| 7. Argumentos de atributo | Feito como payload estruturado, ver abaixo. |

`tests/djinnc/run.sh` passou a procurar `error[` em vez de `error:`.

**JSON.** Dois lados, de propósito separados:

- **Emissão, no compilador:** escrita à mão em `Diagnostics` (`ByteBuffer.append_json_string`). Não depende de lib nem do std,
  então o diagnóstico sai mesmo com o std e as libs desativados.
- **Leitura, no std (`std/util/json.djinn`):** `Json.parse/get/at/kind/as_int/as_text/...` sobre a yyjson 0.13.0 (C, MIT),
  vendorizada em `runtime/third_party/yyjson/` com o wrapper `runtime/json/djinn_json.c`. Entra no `djinn_runtime.lib`
  como membros próprios do arquivo, então só é linkada em programa que usa `Json`. Quem compila o runtime a partir da fonte
  (sem `--runtime-lib`) não a recebe. Teste: `tests/djinnc/std/json_parse.djinn`.

**Runtime em módulos.** `runtime/djinn_runtime.c` foi dividido em `scheduler.c`, `io_file.c`, `io_socket.c`, `diagnostics.c`,
`threads.c`, `console.c`, `filesystem.c` e `process.c`, com o estado compartilhado em `runtime_internal.h`. O seed
`build/release` ainda lista só `djinn_runtime.c` e `logger.c`, então compila o runtime pela fonte apenas depois de um seed
novo; com `--runtime-lib` funciona.

**Resolvido: CRT misturado no JIT (Windows).** `std/logger` falhava em cerca de metade das execuções com stdout em arquivo
ou pipe (exit `0xC0000409`, fast fail 5, parâmetro inválido na CRT); com stdout em `NUL` nunca falhava. O gerador de símbolos do
processo procura em todos os módulos carregados, e `fflush`, `_fileno` e outros vinham do `msvcrt.dll`, que o processo também carrega,
enquanto o `FILE*` vinha do `ucrtbase.dll`. Os dois têm layouts diferentes. O `--run` agora consulta o `ucrtbase.dll` antes do
processo. Não tinha relação com estado estático do logger.

**Custo por estágio** (Windows, `arith.djinn`, média de 3 execuções, máquina de desenvolvimento):

| Estágio | Tempo |
|---|---|
| `--check` | ~65 ms |
| `--no-link` (IR) | ~95 ms |
| `--run` (JIT) | ~75 ms |
| link com `--lld` e runtime pré-compilado | ~140 ms |
| link com clang compilando o runtime | ~840 ms |

Com `--std`: `--check` ~100 ms, JIT ~90 ms, link `--lld` ~200 ms. Os 94 samples e testes do std inteiros pelo JIT levam 20 s. A
economia vem do link com clang e da recompilação do runtime, ~6x; `--lld` com runtime pré-compilado já é barato, e o JIT
fica no mesmo patamar de `--check`. Isso derruba a premissa dos "6 s por teste" do gtest: o custo era o clang do stage0,
não o std.

**Payload de atributos.** O argumento deixa de ser texto solto. `[Stage(Phase.IR | Phase.JIT, label = "both")]` é
checado como um inicializador do struct declarado em `attribute Stage { i32 mask; i8* label; }`:

- Valem argumentos posicionais e nomeados, na mesma regra de `{ .campo = valor }`. Campo desconhecido, argumento a mais ou
  tipo errado reaproveitam os erros do inicializador; valor que não é constante dá `E8005`.
- Cada valor é avaliado pelo `ConstEval` e vira uma constante LLVM, então o payload é consteval: literais, `|`,
  variáveis `consteval`, chamadas de função `consteval`/`constexpr` e as intrínsecas de plataforma, como
  `[Only(Platform.Windows, Runtime.PointerSize)]` (verificado: `samples/attribute_payload_consteval.djinn`). Chamada de
  função comum dá `E8005`.
- A reflexão ganhou `AttributeInfo.payload`, um ponteiro para o struct do atributo, como o `GetCustomAttribute<T>` do C#:
  `Stage* stage = (Stage*)typeof(target).attrs[0].payload;`. `AttributeInfo.value` continua com o texto do literal único.
- O parser agora registra o nome de um `attribute X { ... }` como tipo, para o cast acima funcionar.
- Testes: `SamplesTests.attribute_payload` e `ErrorsTests.attribute_payload_*`.
- **Layout:** `AttributeInfo` passou de 2 para 3 campos em `runtime/core/type_info.djinn` e `std/types/types.djinn`. O stage0
  em C++ emite o layout antigo e não foi tocado; como ele sai de cena, isso só importa se alguém compilar o std com ele.

Pronto quando: todo erro do djinnc tem código (feito), um programa Djinn consegue rodar um filho e ler stdout, stderr e
exit code (feito), existe uma tabela medida de custo por estágio (feito) e `[Requires(IR | JIT)]` chega inteiro à reflexão (feito).

**Fase 0b (runtime async): feita.** `Task.delay(ms)`, `Task.pause()` e `Process.wait_async` sobre o yield do event loop
(ver *Async por padrão*). Verificado: `run.sh` e `fixpoint.sh` passam no Windows.

### Fase 1: runner e conformidade (substitui `run.sh`)

1. `libs/testing` com `djinn.proj` próprio; `libs:` do `djinn.proj` aceita bibliotecas além de `std`.
2. Modelo de nó e resultado, scheduler com pool de processos, relatório no console e em JSON.
3. `djinnc/testing` como sublib: fora de `djinnc/src`, para não entrar no compilador. Como `.djlib` não declara dependências,
   o consumidor lista as duas em `libs:`.
4. Na sublib: `Sources`, `Expected` e `Verify`, e o leitor dos sidecars `.exit` e `.stdout`, que gera um teste por caso antigo.
5. Na sublib: máscara `requires`, dedução pelas chamadas do método, `--require Link` e `--compiler`; o relatório mostra
   quantos casos rodaram em cada caminho.
6. Projeto `tests/conformance`, ligado ao CTest.

Pronto quando (feito): `samples`, `std`, `errors`, `crash` e `debug` rodam pelo runner, em paralelo, e o `run.sh` pode ser
apagado. Os 48 casos de `errors/` ganham códigos esperados.

#### Estado da Fase 1

São três projetos, compilados nesta ordem por `tests/conformance/build.sh`:

| Projeto | Conteúdo | Depende de |
|---|---|---|
| `libs/testing` | `Test`, `TestPolicy`, `TestMethods`, `TestNode`, `Scheduler`, `TestReport`; não sabe o que é um compilador | `std` |
| `djinnc/testing` | `Runner`, `Verify`, `Expected`, `Sources`, comparação de diagnósticos, `TestPath`, `Requires` | `std`, `testing` |
| `tests/conformance` | só o `main` e os testes | `std`, `testing`, `djinnc_testing` |

O `Scheduler` não chama quem o usa, porque a linguagem ainda não tem ponteiro de função nem interface como valor. O controle é
invertido: `wait()` devolve o próximo nó cujo processo terminou, a sublib decide, e chama `proceed(node, comando)` para o próximo
passo ou `complete(node, motivo)` para encerrar. `TestNode.data` aponta para o `TestCase` da sublib.

O build usa o compilador sob teste, porque o seed publicado ainda não lê `libs:`, `sources:` nem `--lib-dir`. Depois de um seed
novo, `ctest.sh` pode compilar o runner com ele. Rode da raiz do repositório:

```bash
DJINN_USE_LLD=1 bash tests/conformance/build.sh <djinnc> <lib> build/runner
build/runner --compiler <djinnc absoluto> --runtime-lib <lib> [--runtime-dir d] [--jobs n] [--filter texto] [--report arquivo.json]
             [--require Check|IR|JIT|Link] [--link-debug] [--crash] [--debug] [--library] [--out <dir absoluto> [--lld]]
```

- As expectativas são código. `djinnc/testing/src/` tem `Sources`, `Expected` e `Verify`; os testes ficam em `tests/conformance/src/suites/`.
  Todo teste tem a source inline: `tests/djinnc/` só guarda o `fixpoint.sh`.
- A descoberta não tem lista: `verify.discover()` percorre `Reflection.type_count()` e `Reflection.type_at(i)`, e roda cada struct que tem um método
  `[Test]`. A lista é genérica: o `main` gerado registra os tipos refletidos no runtime, e o compilador não sabe o que é um teste.

```djinn
struct MathTests {
    [Test]
    void adds() {
        str source = """
i32 main() {
    return 1 + 2;
}
""";
        Verify.source(source).runs(Expected.exit(3));
    }
}
```

- `Verify.source(texto)` e `Verify.with_source(source)` escolhem a entrada. Cada checagem devolve o `Verify`, então a mesma source encadeia
  `.runs(...).crashes(...)` ou `.stops_at(8, ...).stops_at(14, ...)`. Mais de uma checagem no mesmo método ganha o sufixo `#2`, `#3`.
  Um `[Test]` com parâmetros, `async` ou sem nenhuma checagem reprova.
- `.at(linha, coluna)` e `.at_range(linha, coluna, linha_fim, coluna_fim)` localizam um diagnóstico. O JSON traz `end_line` e `end_column`, calculados
  a partir de `length`; por isso um token que ocupa várias linhas ainda sai com o fim na primeira.
- Um `[Test]` só registra casos: cada caso roda depois em um processo próprio. Não há `Expect` dentro do método (item 2 da Fase 3).
- **Falta:** o grafo, que é a Fase 4.

### Fase 2: migração do gtest

Nem todo teste do gtest merece ir para o djinnc: são 723 `TEST` em 54 arquivos, e cada um é avaliado antes de ser portado.
`tests/conformance/port_gtests.py` extrai cada `TEST`, executa no djinnc, escreve `tests/conformance/TRIAGE.md` com a decisão e o
motivo de cada um, e gera as sources e as suítes. Executar vale mais do que ler: um erro "esperado" pode sair por outro motivo,
como `contracts are not supported yet`, e essa lista fica no script.

- **Portar:** passa no djinnc. A source e a expectativa ficam em um `[Test]` em
  `tests/conformance/src/suites/<área>.djinn`.
- **Reescrever:** o comportamento importa, mas o teste precisa de outra forma: confere IR ou usa a API C++.
- **Bloqueado:** o djinnc não reproduz o comportamento. Cada lacuna precisa de uma decisão: implementar, ou tirar o recurso da
  linguagem e descartar os testes.
- **Descartar:** interno do stage0 (mangler, `DiagnosticEngine`) ou duplicado.

Próximos passos:

1. Decidir cada lacuna da tabela de `TRIAGE.md`.
2. Congelar `tests/**/*.cpp`: teste novo só em Djinn.
3. Reescrever os casos marcados, o que depende de `ir_contains` e de um caso multiarquivo.
4. Trocar os casos que só compilam por expectativas mais fortes, aos poucos.

Pronto quando: todo `TEST` tem decisão na triagem e o gtest deixa de ser necessário para validar o djinnc. O stage0 pode manter o dele até ser aposentado.

### Fase 3: `[Test]` e `djinnc test`

`[Test]` já existe no runner de conformidade, por reflexão (veja o estado da Fase 1). O que segue é para testes de projeto, com `djinnc test`.

1. Em modo teste, o compilador emite uma tabela de descritores (nome, arquivo, linha, atributos, `MethodInfo*`) e um
   thunk por teste, `void(ptr* args, ptr out)`, porque o runner não pode chamar uma assinatura desconhecida. Thunks de `async` fazem `await`.
2. `Expect`/`Assert` e o gancho de panic no runtime.
3. Subcomando `djinnc test [dir]`: compila o projeto incluindo `*_test.djinn`, gera o entrypoint se não houver `main`,
   linka um binário só e executa. Workers são esse mesmo binário reexecutado, então é um link por projeto, não por teste.
   Flags: `--filter`, `--tag`, `--jobs`, `--list`, `--report`, `--fail-fast`. `djinnc arquivo.djinn` continua funcionando.
4. `*_test.djinn` não entra em build normal. Isso cobre tanto testes ao lado do código quanto um projeto só de testes.
5. Migrar `SelfTest` para `djinnc/src/**/*_test.djinn` e remover `--selftest`.

Pronto quando: `djinnc test djinnc` roda os antigos checks de arena, atoms e mapas.

### Fase 4: grafo de dados

1. Ligação por tipo, `[From]`, validador (sem produtor, ambíguo, ciclo) com diagnóstico que aponta a declaração.
2. Execução com passagem de valores, memoização por função + ligações, estado `Blocked`, componentes conexos como unidade de agendamento.
3. API fluente: `TestPlan.discover()`, `workflow()`, `step()`, filtro `--workflow`.
4. `[Mutates]`.
5. `Artifact` (saída em arquivo) e `--graph` em DOT e JSON, com o estado de cada nó.
6. Primeiro uso real: reescrever `tests/djinnc/library/run.cmd` e `tools/bootstrap.sh` (`stage1 → stage2 → stage3 → fixpoint`) como workflows.
7. Na sublib: o resultado da compilação (`Compilation`) passa a ser a saída de um nó, para um teste de IR ou de execução reaproveitar a compilação de um teste de diagnóstico da mesma source.

Pronto quando: o bootstrap roda como workflow, e o exemplo do marketplace executa `login` uma vez para vários workflows
e mostra `Blocked` quando ele falha.

### Fase 5: ergonomia

Por ordem de valor: timeout por nó e `[Skip]`/`[Tag]`; setup e teardown (nó que devolve recurso com `destroy`); snapshots
de stdout, AST e diagnósticos com `--update-snapshots`; testes parametrizados; JUnit XML; `[Resource("db")]` para
serializar quem disputa o mesmo recurso externo; `--seed`; `--watch`.

### Fase 6: benchmarks nativos

`[Bench]` no mesmo grafo: uma saída de teste pode preparar a entrada do benchmark. Aquecimento, repetições, relatório
JSON e comparação com referência. Substitui o passo 5 do `bootstrap.sh` e o `tools/bench_scale.sh`. Benchmarks rodam em
série, nunca junto com testes, para não medir ruído.

## Depois, se fizer falta

| Ideia | Quando vale |
|---|---|
| Cobertura | Primeiro candidato: instrumentação do LLVM, sem inventar formato próprio. |
| Pular testes não afetados (`--changed`) | Depois da cobertura; o grafo e o hash de fontes dão a base. |
| Threads reais no runtime | Quando houver ramos pesados em CPU; exige alimentar `pool.queue` e shadow stack por thread. |
| Saídas `Serializable` entre workers | Alternativa às threads para paralelizar ramos. |
| Property-based e fuzz | Útil no parser; precisa de geradores por `#fields` e de shrinking. |
| Mocks por interface | Gerar via `$inspect ... generate`, só para interfaces. |
| Testes de arquitetura | Regras sobre o grafo de imports, que o compilador já conhece. |
| Mutation testing | Caro; só depois de cobertura e seleção de testes. |

Fora do escopo: engine de grafos genérico como produto, feature flags e testes A/B, sistema de plugins, time travel e
testes distribuídos. As conversas sugeriam começar pelo engine genérico; aqui é o contrário, porque ainda não existe um
segundo consumidor para definir essa abstração. O scheduler fica sem conceitos de teste, para poder ser extraído depois.

## Riscos

- **Porte revela divergências entre stage0 e djinnc.** Provável na Fase 2, e é um ganho, mas o prazo depende da triagem.
- **Testes encadeados ficam acoplados.** O teste isolado continua sendo o padrão; o encadeamento é para fluxos reais.
- **Componente gigante.** Se tudo depender de `login`, o paralelismo some. `--graph` deixa isso visível.
- **Custo por teste.** Evitar o link corta o clang, mas não a compilação do std em casos com `--std`. Se a medição da
  Fase 0 mostrar que esse é o gargalo, std como `.djlib` vira pré-requisito da Fase 2.
- **`JIT` mascarar bug de AOT.** Mitigado pelo `--require Link` no CI noturno e por `JIT | Link` nos casos sensíveis.
- **Biblioteca sobre biblioteca.** Funciona, mas `.djlib` não tem lista de dependências: cada consumidor lista todas em `libs:`.

## Decisões em aberto

| Decisão | Recomendação |
|---|---|
| Sintaxe de `[Requires]` | `[Requires(IR \| JIT)]`, com a mudança no parser; a alternativa sem mudança é `[Requires("IR\|JIT")]`, que já funciona hoje |
| Estilo dos asserts | `Expect.equal(a, b)` estático, em vez de `expect(a).to_equal(b)`: menos alocação e mensagens mais diretas |
| Códigos de erro | Reaproveitar os do stage0 onde houver equivalente |
| Subcomandos | Adotar `djinnc test` agora, abrindo caminho para `build`, `bench` e `fmt` |
