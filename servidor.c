/*
 * servidor.c - Pedra, Papel e Tesoura com Apostas
 * Compilar: gcc servidor.c -o servidor.exe -lws2_32 -lpthread
 */
#include <winsock2.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "protocolo.h"

/* ---------- Memoria compartilhada ---------- */
static Estado jogo;                                   // estado real do jogo
static pthread_mutex_t mutex_jogo  = PTHREAD_MUTEX_INITIALIZER; // protege "jogo"
static pthread_cond_t  cond_jogo   = PTHREAD_COND_INITIALIZER;  // espera das duas jogadas
static pthread_mutex_t mutex_envio = PTHREAD_MUTEX_INITIALIZER; // evita send intercalado no mesmo socket

static SOCKET socks[3];                               // indices 1 e 2

/* ---------- Utilitarios ---------- */
// Traducao das mensagens, para o log ficar legivel
static const char *traduzir_saida(const char *m) {
    if (!strcmp(m, "X01")) return "erro: aposta maior que o saldo/limite";
    if (!strcmp(m, "X02")) return "erro: nao e a sua vez";
    if (!strcmp(m, "X03")) return "erro: jogada invalida";
    if (!strcmp(m, "X04")) return "erro: acao fora da fase correta";
    if (!strcmp(m, "X05")) return "erro: aposta menor que 1";
    if (!strcmp(m, "X06")) return "erro: voce ja jogou nesta rodada";
    switch (m[0]) {
        case 'B': return "boas-vindas (id do jogador)";
        case 'W': return "aguardando outro jogador";
        case 'V': return "vez de apostar";
        case 'C': return "cobrir a aposta";
        case 'G': return "liberado para jogar";
        case 'S': return "saldos e pote";
        case 'R': return "resultado da rodada";
        case 'F': return "fim de jogo";
    }
    return "mensagem desconhecida";
}

static const char *traduzir_entrada(const char *m) {
    if (strlen(m) < 2) return "mensagem invalida";
    switch (toupper((unsigned char)m[1])) {
        case 'E': return "entrar no jogo";
        case 'A': return "apostar/cobrir";
        case 'J': return "jogada (1 pedra, 2 papel, 3 tesoura)";
        case 'Q': return "sair do jogo";
    }
    return "comando desconhecido";
}

static void log_msg(const char *dir, int id, const char *msg) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    const char *txt = (dir[0] == '-') ? traduzir_saida(msg) : traduzir_entrada(msg);
    printf("[%02d:%02d:%02d] %s J%d: %-12s (%s)\n",
           tm->tm_hour, tm->tm_min, tm->tm_sec, dir, id, msg, txt);
    fflush(stdout);
}

// Envia msg + '\n'. NUNCA chamar com mutex_jogo travado.
static void enviar(int id, const char *msg) {
    char buf[TAM_MSG + 2];
    snprintf(buf, sizeof(buf), "%s\n", msg);
    pthread_mutex_lock(&mutex_envio);
    send(socks[id], buf, (int)strlen(buf), 0);
    pthread_mutex_unlock(&mutex_envio);
    log_msg("->", id, msg);
}

static void broadcast(const char *msg) {
    enviar(1, msg);
    enviar(2, msg);
}

static void enviar_erro(int id, int cod) {
    char m[8];
    snprintf(m, sizeof(m), "X%02d", cod);
    enviar(id, m);
}

// Le uma linha terminada em '\n' (TCP e fluxo). Retorna -1 se desconectou.
static int recv_linha(SOCKET s, char *buf, int tam) {
    int n = 0;
    char c;
    while (n < tam - 1) {
        int r = recv(s, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\n') break;
        if (c != '\r') buf[n++] = c;
    }
    buf[n] = '\0';
    return n;
}

// Monta "S1:x,2:y,P:z". Deve ser chamada COM o mutex travado.
static void montar_saldo(char *out, size_t tam) {
    snprintf(out, tam, "S1:%d,2:%d,P:%d", jogo.fichas[1], jogo.fichas[2], jogo.pote);
}

// Le o estado dentro do mutex e envia para os dois (envio fora da regiao critica).
static void enviar_saldo(void) {
    char m[TAM_MSG];
    pthread_mutex_lock(&mutex_jogo);
    montar_saldo(m, sizeof(m));
    pthread_mutex_unlock(&mutex_jogo);
    broadcast(m);
}

// 0 empate; 1 se a vence; 2 se b vence
static int vencedor(int a, int b) {
    if (a == b) return 0;
    if ((a == PEDRA && b == TESOURA) || (a == PAPEL && b == PEDRA) || (a == TESOURA && b == PAPEL))
        return 1;
    return 2;
}

/* ---------- Fase de aposta ---------- */
static void tratar_aposta(int id, const char *arg) {
    int outro = (id == 1) ? 2 : 1;
    int erro = 0, pediu_cobrir = 0, liberou_jogada = 0;
    int valor = isdigit((unsigned char)arg[0]) ? atoi(arg) : 0;
    char msg_cobrir[TAM_MSG] = "", msg_saldo[TAM_MSG] = "";

    pthread_mutex_lock(&mutex_jogo);
    int limite = jogo.fichas[1] < jogo.fichas[2] ? jogo.fichas[1] : jogo.fichas[2];

    if (!jogo.ativo || (jogo.fase != FASE_APOSTA && jogo.fase != FASE_COBRIR)) erro = ERR_FASE_ERRADA;
    else if (jogo.vez != id)                                 erro = ERR_NAO_E_SUA_VEZ;
    else if (valor < 1)                                      erro = ERR_APOSTA_MENOR_1;
    else if (valor > limite)                                 erro = ERR_APOSTA_INVALIDA;
    else if (jogo.fase == FASE_COBRIR && valor != jogo.aposta[outro]) erro = ERR_APOSTA_INVALIDA;
    else if (jogo.fase == FASE_APOSTA) {
        // abridor aposta
        jogo.aposta[id] = valor;
        jogo.fase = FASE_COBRIR;
        jogo.vez = outro;
        pediu_cobrir = 1;
        snprintf(msg_cobrir, sizeof(msg_cobrir), "C%d:%d", outro, valor);
    } else {
        // oponente cobriu
        jogo.aposta[id] = valor;
        jogo.fichas[1] -= valor;
        jogo.fichas[2] -= valor;
        jogo.pote += 2 * valor;
        jogo.fase = FASE_JOGADA;
        jogo.vez = 0;
        liberou_jogada = 1;
        montar_saldo(msg_saldo, sizeof(msg_saldo));
    }
    pthread_mutex_unlock(&mutex_jogo);

    if (erro)                enviar_erro(id, erro);
    else if (pediu_cobrir)   broadcast(msg_cobrir);   // cliente 'outro' deve cobrir; o abridor so ve o aviso
    else if (liberou_jogada) { broadcast(msg_saldo); broadcast("G"); }
}

/* ---------- Fase de jogada ---------- */
static void tratar_jogada(int id, const char *arg) {
    int outro = (id == 1) ? 2 : 1;
    int n = isdigit((unsigned char)arg[0]) ? atoi(arg) : 0;
    int erro = 0, resolver = 0;
    char m_res[TAM_MSG] = "", m_saldo[TAM_MSG] = "", m_prox[TAM_MSG] = "";

    pthread_mutex_lock(&mutex_jogo);
    if (!jogo.ativo || jogo.fase != FASE_JOGADA) erro = ERR_FASE_ERRADA;
    else if (n < PEDRA || n > TESOURA)           erro = ERR_JOGADA_INVALIDA;
    else if (jogo.jogada[id] != 0)               erro = ERR_JA_JOGOU;

    if (!erro) {
        jogo.jogada[id] = n;
        if (jogo.jogada[outro] == 0) {
            // primeiro a jogar: espera o outro
            while (jogo.ativo && jogo.fase == FASE_JOGADA && jogo.jogada[outro] == 0)
                pthread_cond_wait(&cond_jogo, &mutex_jogo);
            // quem acordou nao faz mais nada: o segundo jogador resolveu a rodada
        } else {
            // segundo a jogar: calcula o resultado
            resolver = 1;
            int j1 = jogo.jogada[1], j2 = jogo.jogada[2];
            int venc = vencedor(j1, j2);
            int pote_rodada = jogo.pote;
            snprintf(m_res, sizeof(m_res), "R%d,%d,%d,%d", venc, j1, j2, pote_rodada);

            if (venc != 0) {
                jogo.fichas[venc] += jogo.pote;
                jogo.pote = 0;
            } else if (jogo.fichas[1] == 0 || jogo.fichas[2] == 0) {
                // empate com alguem zerado: devolve o pote dividido, senao o jogo travaria
                jogo.fichas[1] += jogo.pote / 2;
                jogo.fichas[2] += jogo.pote / 2;
                jogo.pote = 0;
            } // empate normal: pote acumula

            montar_saldo(m_saldo, sizeof(m_saldo));

            if (jogo.fichas[1] == 0 || jogo.fichas[2] == 0) {
                int campeao = jogo.fichas[1] > 0 ? 1 : 2;
                jogo.ativo = 0;
                jogo.fase = FASE_FIM;
                snprintf(m_prox, sizeof(m_prox), "F%d", campeao);
            } else {
                jogo.rodada++;
                jogo.aposta[1] = jogo.aposta[2] = 0;
                jogo.jogada[1] = jogo.jogada[2] = 0;
                jogo.vez = (jogo.rodada % 2 == 1) ? 1 : 2;   // alterna quem abre
                jogo.fase = FASE_APOSTA;
                snprintf(m_prox, sizeof(m_prox), "V%d", jogo.vez);
            }
            pthread_cond_broadcast(&cond_jogo);  // acorda o outro que esta no cond_wait
        }
    }
    pthread_mutex_unlock(&mutex_jogo);

    if (erro) { enviar_erro(id, erro); return; }
    if (resolver) {
        broadcast(m_res);
        broadcast(m_saldo);
        broadcast(m_prox);
    }
}

/* ---------- Desconexao / W.O. ---------- */
static void desconectar(int id) {
    int outro = (id == 1) ? 2 : 1;
    int avisar = 0;
    char m[8];

    pthread_mutex_lock(&mutex_jogo);
    if (jogo.ativo) {
        jogo.ativo = 0;
        jogo.fase = FASE_FIM;
        avisar = 1;
        pthread_cond_broadcast(&cond_jogo);   // libera thread presa no cond_wait
    }
    pthread_mutex_unlock(&mutex_jogo);

    if (avisar) {
        snprintf(m, sizeof(m), "F%d", outro);  // vitoria por W.O.
        enviar(outro, m);
    }
}

/* ---------- Thread de cada cliente ---------- */
static void *thread_cliente(void *arg) {
    int id = *(int *)arg;
    free(arg);
    char linha[TAM_MSG];

    while (1) {
        int n = recv_linha(socks[id], linha, sizeof(linha));
        if (n < 0) { desconectar(id); break; }
        if (n < 2) continue;                    // lixo: ignora
        log_msg("<-", id, linha);

        // linha[0] = id declarado pelo cliente (usamos o id da thread)
        char cmd = toupper((unsigned char)linha[1]);
        const char *resto = linha + 2;

        if (cmd == 'E')       { /* B<id> ja foi enviado no accept */ }
        else if (cmd == 'A')  tratar_aposta(id, resto);
        else if (cmd == 'J')  tratar_jogada(id, resto);
        else if (cmd == 'Q')  { desconectar(id); break; }
        // outros comandos: ignora

        pthread_mutex_lock(&mutex_jogo);
        int fim = !jogo.ativo;
        pthread_mutex_unlock(&mutex_jogo);
        if (fim) break;
    }
    return NULL;
}

/* ---------- main ---------- */
int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("Erro WSAStartup\n"); return 1; }

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { printf("Erro socket\n"); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORTA);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) { printf("Erro bind\n"); return 1; }
    if (listen(srv, 2) == SOCKET_ERROR) { printf("Erro listen\n"); return 1; }

    // estado inicial
    memset(&jogo, 0, sizeof(jogo));
    jogo.fichas[1] = jogo.fichas[2] = FICHAS_INICIAIS;
    jogo.fase = FASE_AGUARDANDO;
    jogo.vez = 1;
    jogo.rodada = 1;
    jogo.ativo = 1;

    printf("Servidor na porta %d. Aguardando jogadores...\n", PORTA);

    pthread_t th[3];
    for (int id = 1; id <= 2; id++) {
        socks[id] = accept(srv, NULL, NULL);
        if (socks[id] == INVALID_SOCKET) { printf("Erro accept\n"); return 1; }

        char m[8];
        snprintf(m, sizeof(m), "B%d", id);
        enviar(id, m);
        if (id == 1) enviar(1, "W");

        pthread_mutex_lock(&mutex_jogo);
        jogo.conectados++;
        pthread_mutex_unlock(&mutex_jogo);

        int *parg = malloc(sizeof(int));
        *parg = id;
        pthread_create(&th[id], NULL, thread_cliente, parg);
    }

    // os dois conectaram: libera a fase de aposta
    pthread_mutex_lock(&mutex_jogo);
    jogo.fase = FASE_APOSTA;
    jogo.vez = 1;
    pthread_mutex_unlock(&mutex_jogo);

    enviar_saldo();
    broadcast("V1");

    pthread_join(th[1], NULL);
    pthread_join(th[2], NULL);

    closesocket(socks[1]);
    closesocket(socks[2]);
    closesocket(srv);
    WSACleanup();
    printf("Jogo encerrado.\n");
    return 0;
}
