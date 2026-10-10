# 🪨📄✂️ JokenPo

Pedra, Papel e Tesoura com apostas para dois jogadores, feito em C com sockets e threads.

> 🚧 Projeto em desenvolvimento

---

# 📌 Sobre o projeto

Trabalho 02 do 2º bimestre da disciplina de **Sistemas Operacionais**, do curso de **Engenharia de Software** (4º semestre), ministrada pelo **Prof. Maurilio Campano Jr**.

O objetivo é implementar um jogo multiplayer com **troca de mensagens** entre processos e **gerenciamento de memória compartilhada com exclusão mútua**.

---

# 🎮 Como funciona o jogo

1. Cada jogador começa com 100 fichas.
2. Um jogador aposta e o outro cobre. As apostas vão para o **pote**.
3. Os dois escolhem pedra, papel ou tesoura sem ver a jogada do oponente.
4. O vencedor leva o pote. Em caso de empate, o pote acumula para a próxima rodada.
5. O jogo termina quando um jogador fica sem fichas.

**Regras de aposta**

- Quem abre a rodada alterna a cada rodada (jogador 1, jogador 2, jogador 1...).
- Aposta mínima: 1 ficha.
- Aposta máxima: o menor saldo entre os dois jogadores, para que ninguém aposte mais do que o outro consegue cobrir.
- O oponente deve cobrir com exatamente o mesmo valor.

---

# 📌 Pré-requisitos

- **Windows** (usa Winsock2)
- **GCC via MinGW-w64** com suporte a Pthreads (winpthreads) ([MSYS2](https://www.msys2.org/) é uma forma simples de instalar)
- Porta **5000** livre

Verifique a instalação com:

```bash
gcc --version
```

---

# 📂 Estrutura de arquivos

```text
JokenPo/
├── protocolo.h    # Constantes, fases do jogo, códigos de erro e struct Estado (usado por servidor e cliente)
├── servidor.c     # Servidor: guarda o estado real do jogo, valida as jogadas e responde aos clientes
└── README.md      # Documentação do projeto
```

### `protocolo.h`

Contrato compartilhado entre servidor e cliente. Não contém lógica, apenas definições:

- Constantes do jogo (`PORTA`, `FICHAS_INICIAIS`, `PEDRA`, `PAPEL`, `TESOURA`).
- Fases da rodada (`FASE_AGUARDANDO`, `FASE_APOSTA`, `FASE_COBRIR`, `FASE_JOGADA`, `FASE_FIM`).
- Códigos de erro (`ERR_*`).
- A `struct Estado`, que representa a memória compartilhada do servidor.

### `servidor.c`

Responsável por toda a regra do jogo:

- Abrir o socket, aceitar exatamente 2 clientes e criar **uma thread por cliente**.
- Manter o `Estado` global (fichas, pote, apostas, jogadas, vez e fase) protegido por mutex.
- Validar cada mensagem (fase correta, vez do jogador, valor válido) e responder com erro quando necessário.
- Sincronizar as duas jogadas com variável de condição, calcular o vencedor e preparar a próxima rodada.
- Detectar desconexão e encerrar a partida com vitória por W.O.
- Registrar no console tudo que entra e sai, com horário.

---

# 🏗️ Arquitetura

```text
Cliente 1 --socket--+
                    +-- Servidor (1 thread por cliente + mutex + cond + struct Estado)
Cliente 2 --socket--+
```

- O **servidor** é o dono do estado e valida tudo.
- Cada **cliente** mantém apenas uma cópia local (vez, fase, fichas e pote), atualizada exclusivamente pelas mensagens do servidor.
- Comunicação via **TCP**, na porta `5000`, com mensagens em texto terminadas em `\n`.

### Fluxo de uma rodada

```text
APOSTA  ->  COBRIR  ->  JOGADA  ->  resultado  ->  APOSTA (próxima rodada) ou FIM
```

---

# 📡 Protocolo de mensagens

Toda mensagem termina com `\n`. O servidor lê o fluxo TCP até encontrar o `\n`, então mensagens quebradas ou coladas são tratadas corretamente.

### Cliente para Servidor

| Mensagem | Significado | Exemplo |
|---|---|---|
| `<id>E` | Entrar no jogo (o servidor ignora, o `B<id>` já é enviado ao conectar) | `1E` |
| `<id>A<valor>` | Apostar (abridor) ou cobrir (oponente) | `1A30` |
| `<id>J<n>` | Jogada: 1 = pedra, 2 = papel, 3 = tesoura | `2J3` |
| `<id>Q` | Sair do jogo | `1Q` |

### Servidor para Cliente

| Mensagem | Significado | Exemplo |
|---|---|---|
| `B<id>` | Boas-vindas, informa o id do jogador | `B1` |
| `W` | Aguardando o outro jogador conectar | `W` |
| `V<id>` | Vez de apostar do jogador `id` (abre a rodada) | `V1` |
| `C<id>:<valor>` | O jogador `id` deve cobrir a aposta de `valor` (enviada aos dois jogadores) | `C2:30` |
| `G` | Liberado para jogar (os dois escolhem) | `G` |
| `S1:<f1>,2:<f2>,P:<pote>` | Saldos e pote atualizados | `S1:70,2:70,P:60` |
| `R<venc>,<j1>,<j2>,<pote>` | Resultado da rodada (`venc`: 0 = empate, 1 ou 2) | `R1,1,3,60` |
| `F<venc>` | Fim de jogo, `venc` é o campeão | `F2` |
| `X<cod>` | Erro | `X01` |

### Códigos de erro

| Código | Motivo |
|---|---|
| `X01` | Aposta maior que o saldo ou o limite (ou valor diferente do que deve ser coberto) |
| `X02` | Não é a sua vez |
| `X03` | Jogada inválida (fora de 1 a 3) |
| `X04` | Ação fora da fase correta |
| `X05` | Aposta menor que 1 |
| `X06` | Você já jogou nesta rodada |

### Exemplo de rodada completa

```text
C1 -> S : 1E            S -> C1 : B1  e depois W
C2 -> S : 2E            S -> C2 : B2
S -> todos : S1:100,2:100,P:0
S -> todos : V1
C1 -> S : 1A30
S -> todos : C2:30      (jogador 2 deve cobrir)
C2 -> S : 2A30
S -> todos : S1:70,2:70,P:60
S -> todos : G
C1 -> S : 1J1           (pedra, escondida do outro)
C2 -> S : 2J3           (tesoura)
S -> todos : R1,1,3,60
S -> todos : S1:130,2:70,P:0
S -> todos : V2         (próxima rodada, agora o jogador 2 abre)
```

> O servidor nunca repassa a jogada de um jogador antes de receber a do outro.

---

# 🔒 Exclusão mútua e sincronização

O servidor cria uma thread por cliente e todas acessam a mesma `struct Estado` global, que funciona como a memória compartilhada do jogo.

| Recurso | Função |
|---|---|
| `mutex_jogo` (`pthread_mutex_t`) | Protege toda leitura e escrita do `Estado`, evitando condições de corrida em saldos e pote |
| `cond_jogo` (`pthread_cond_t`) | O primeiro jogador a jogar espera (`pthread_cond_wait`, sempre dentro de um `while`) até o segundo jogar; o segundo calcula o resultado e acorda o primeiro (`pthread_cond_broadcast`) |
| `mutex_envio` (`pthread_mutex_t`) | Impede que dois `send` no mesmo socket se misturem |

**Padrão usado em toda alteração de estado:**

```c
pthread_mutex_lock(&mutex_jogo);
// valida e altera jogo.*
// monta as mensagens em variáveis locais
pthread_mutex_unlock(&mutex_jogo);
// somente agora envia pela rede
```

Operações de rede (`send`/`recv`) nunca são feitas com `mutex_jogo` travado, para evitar bloqueios.

---

# ▶️ Como compilar e executar

### Compilar (MinGW)

```bash
gcc servidor.c -o servidor.exe -lws2_32 -lpthread
gcc cliente.c -o cliente.exe -lws2_32 -lpthread
```

> Em IDEs como o Dev-C++, adicione `-lws2_32 -lpthread` nas opções do linker. Sem isso ocorre erro de `undefined reference`.

### Executar

Ordem: servidor, depois os dois clientes.

```bash
.\servidor.exe
.\cliente.exe    # jogador 1
.\cliente.exe    # jogador 2
```

Para jogar em máquinas diferentes, o cliente deve apontar para o IP do servidor na rede local e a porta `5000` deve estar liberada no firewall.

---

# 🧪 Testando o servidor sem o cliente

É possível testar o servidor usando `telnet` (ou `nc`) em dois terminais:

```bash
telnet localhost 5000
```

Exemplo de teste de uma rodada (digite as linhas e pressione Enter):

| Terminal | Digite | Esperado |
|---|---|---|
| 1 e 2 | (conectar) | `B1`, `W`, `B2`, `S1:100,2:100,P:0`, `V1` |
| 1 | `1A30` | `C2:30` |
| 2 | `2A30` | `S1:70,2:70,P:60`, `G` |
| 1 | `1J1` | (aguarda o outro jogar) |
| 2 | `2J3` | `R1,1,3,60`, `S1:130,2:70,P:0`, `V2` |

Testes de erro:

| Digite | Esperado |
|---|---|
| `2A30` fora da vez | `X02` |
| `1A999` | `X01` |
| `1A0` | `X05` |
| `1J1` fora da fase de jogada | `X04` |

---

# 📝 Decisões de projeto

- **Mensagem `E`:** o cliente ainda não conhece seu id ao entrar, então o servidor envia `B<id>` assim que aceita a conexão e ignora o `E`.
- **Mensagem `C<id>:<valor>`:** é enviada aos dois jogadores. Cada cliente compara o `id` com o seu para saber se deve cobrir.
- **Empate com algum jogador sem fichas:** se os dois apostam tudo e empatam, o pote acumulado deixaria o jogo sem apostas possíveis. Nesse caso específico o pote é dividido igualmente entre os dois. Em empates normais, o pote acumula.
- **Desconexão:** se um jogador cai ou envia `Q`, o outro recebe `F<id>` e vence por W.O.
- **Logs:** o servidor imprime no console cada mensagem recebida (`<-`) e enviada (`->`), com horário.

---

# 🚧 Status

- [x] Protocolo definido
- [x] Servidor: conexão, estado compartilhado, apostas, jogadas, resultado e fim de jogo
- [x] Servidor: desconexão com vitória por W.O.
- [ ] Cliente
- [ ] Testes de integração servidor + cliente
- [ ] Extras (all-in, melhor de N rodadas, placar de vitórias)

---

# 👥 Autores

- **João Vitor Bolognezi Portela**
- **Lucas França Peloi**
